// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/**
 * Wii U frontend for chiaki-ng.
 *
 * Fase 1 (validada en Cemu + consola real): smoke test, discovery de
 *   consolas, GX2 via WHBGfx (clear dentro del pass TV+DRC), VPAD, red de
 *   Cafe OS, threads OSThread, mutex/cond, select/stoppipe UDP loopback y
 *   salida limpia con HOME.
 * Fase 2: registro con PIN (chiaki_regist) y sesion de Remote Play
 *   (chiaki_session: session request TCP 9295, ctrl, takion UDP, senkusha,
 *   FEC). El Account ID de PSN se introduce a mano en base64.
 *   Sin decoder aun: los frames de video se descartan y se contabilizan
 *   (valida FEC/gf-complete en la ruta real); el decode llega en fase 3.
 *
 * Controles:
 *   X = configurar IP de la consola (teclado del sistema)
 *   Y = borrar IP y volver a broadcast 255.255.255.255
 *   ↑ = registrar consola (ID PSN + PIN de "Enlazar con otro dispositivo")
 *   SELECT = editar el Account ID de PSN en cualquier momento
 *   A = conectar Remote Play (despierta la PS4 si esta en standby)
 *   B = durante la conexion de la sesion, cancela. Con stream activo
 *       (fase 3c) el mando va al juego: layout por posicion fisica
 *       (B=cross, A=circulo, Y=cuadrado, X=triangulo; ZL/ZR=L2/R2,
 *       MINUS=boton PS, PLUS=options) y la sesion se para tocando el
 *       icono de apagado de la esquina inferior derecha.
 *
 * Los resultados van a la consola cafe (OSReport -> log de Cemu), al
 * UDP 4405 y al archivo de la SD (fs:/vol/external01/wiiu/apps/chiaki/
 * log.txt). Las claves de registro viven solo en RAM (persistencia del
 * archivo de configuracion en una fase posterior).
 */

#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/crash.h>
#include <whb/gfx.h>
#include <vpad/input.h>
#include <nsysnet/_socket.h>
#include <coreinit/time.h>
#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/mutex.h>
#include <sndcore2/core.h>
#include <nn/swkbd.h>

#include <chiaki/log.h>
#include <chiaki/thread.h>
#include <chiaki/discovery.h>
#include <chiaki/discoveryservice.h>
#include <chiaki/regist.h>
#include <chiaki/session.h>
#include <chiaki/controller.h>
#include <chiaki/base64.h>
#include <chiaki/opusdecoder.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "video.h"
#include "config.h"
#include "gui.h"
#include "audio.h"

#define DISCOVERY_PING_MS 500
#define DISCOVERY_HOSTS_MAX 8
#define DISCOVERY_DROP_PINGS 3

static ChiakiLog g_chiaki_log;
static ChiakiDiscoveryService g_discovery_service;
static bool g_discovery_running = false;

// IP dirigida de la consola; vacío = modo broadcast.
static char g_host_ip[16] = { 0 };

// Consola descubierta (snapshot desde el cb del servicio; protegido por
// g_host_mutex porque el cb corre en el thread del discovery service).
static ChiakiMutex g_host_mutex;
static bool g_host_found = false;
static char g_host_addr[48] = { 0 };
static char g_host_nickname[0x20] = { 0 };
static uint16_t g_host_request_port = 0; // informativo; chiaki usa 9295
static ChiakiDiscoveryHostState g_host_state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
static ChiakiTarget g_host_target = CHIAKI_TARGET_PS4_UNKNOWN;

// Registro de la consola (clave solo en RAM por ahora).
static bool g_registered = false;
static char g_regist_key[CHIAKI_SESSION_AUTH_SIZE] = { 0 };
static uint8_t g_morning[0x10] = { 0 };

// Cuenta PSN (necesaria para registrar PS4 >= 7.0).
static bool g_have_psn_account = false;
static uint8_t g_psn_account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE] = { 0 };
static char g_psn_online_id[64] = { 0 };

// Persistencia (fase 4a): consolas registradas y cuenta en
// sd:/wiiu/apps/chiaki/config.ini. g_config se toca con g_host_mutex
// (el cb de regist corre en su thread); g_config_dirty lo vacia el main.
static Config g_config;
static volatile bool g_config_dirty = false;
// Consola resaltada en el menu (indice en g_config.consoles); la
// navegan Izq/Dcha y la aplica apply_console() a los globals de sesion.
static int g_menu_sel = 0;
static char g_regist_target_host[CONFIG_HOST_SIZE]; // host del regist en curso

// Dialogo modal de confirmacion de borrado (PLUS en la toolbar): sin
// navegacion — A confirma y B vuelve (como los dialogs de consola).
static bool g_confirm_del = false;

// Registro en curso (chiaki_regist lanza su propio thread).
enum class RegistState { Idle, Running, Finished };
static ChiakiRegist g_regist;
static RegistState g_regist_state = RegistState::Idle;

// Sesion de Remote Play (chiaki_session lanza su propio thread).
static ChiakiSession g_session;
static bool g_session_active = false;
static volatile bool g_session_quit_received = false;
// IP del destino de la sesion en curso (overlay "Conectando...") y
// resultado de la ultima sesion (linea en el pie del menu).
static char g_session_ip[48] = { 0 };
static char g_last_session_msg[96] = { 0 };
static volatile bool g_session_login_pin_requested = false;
static volatile bool g_session_login_pin_incorrect = false;
static volatile uint64_t g_video_frames = 0;
static volatile uint64_t g_video_bytes = 0;
static volatile uint64_t g_video_frames_lost = 0;
static volatile uint64_t g_fec_failures = 0;
static uint64_t g_video_log_ticks = 0;
static uint64_t g_video_log_frames = 0;
// Detector de estancamiento del decoder h264 (fase 3b: congelados con
// wifi 2.4 GHz). Si entran frames pero el hw no decodifica ninguno
// durante ~0.7 s, las referencias H.264 se perdieron en una rafaga de
// perdida y se pide un keyframe (IDR) al instante en vez de esperar
// al siguiente periodico de la PS4.
static uint64_t g_stall_fed = 0;
static uint64_t g_stall_decoded = 0;
static uint64_t g_stall_last_fed_ticks = 0;
static uint64_t g_stall_last_dec_ticks = 0;
static uint64_t g_stall_last_idr_ticks = 0;
static uint64_t g_stall_idr_sent_ticks = 0; // 0 = sin IDR pendiente
// Vueltas del bucle principal (diagnostico de fps: si presented va por
// detras de decoded, aqui se ve si el bucle es quien no da abasto).
static volatile uint64_t g_main_iters = 0;
static uint64_t g_video_log_presented = 0;
static uint64_t g_video_log_iters = 0;

// Zona táctil tipo touchpad de la PS4 (esquina superior derecha del
// DRC). La GamePad solo tiene UN punto táctil, asi que va a un solo
// contacto: arrastre = deslizar, toque corto y quieto = click del
// touchpad (tap-to-click). El contacto se reinserta cada frame en
// session_send_input porque el estado se reconstruye desde reposo.
#define PS_TP_MAXX 1920 // igual que PS_TOUCHPAD_MAXX del escritorio
#define PS_TP_MAXY 1079 // igual que PS_TOUCHPAD_MAXY del escritorio
// Antitemblor: el tactil resistive de la GamePad baila unos px con el
// dedo quieto y el mapeo al pad lo amplifica; se ignora movimiento de
// menos de esta distancia (en px de la zona) del punto establecido.
#define TP_JITTER_PX 3
static bool g_tp_down = false;      // contacto vigente en la zona
static uint16_t g_tp_x = 0;         // pos mapeada al pad (x)
static uint16_t g_tp_y = 0;         // pos mapeada al pad (y)
static uint8_t g_tp_id = 0;         // id del contacto vigente
static uint8_t g_tp_id_next = 0;    // ids unicos por contacto (mask 0x7f)
static bool g_tp_moved = false;     // supero el umbral de tap
static int g_tp_lx0 = 0;            // pos inicial dentro de la zona (x)
static int g_tp_ly0 = 0;            // pos inicial dentro de la zona (y)
static float g_tp_ax = 0.0f;       // punto estable que sigue la salida (x)
static float g_tp_ay = 0.0f;       // punto estable que sigue la salida (y)
static uint64_t g_tp_down_ticks = 0; // instante del contacto (tap)
static uint64_t g_tp_click_ticks = 0; // deadline del pulso de click
static bool g_tp_diag = false;      // primer contacto de sesion logueado
// Surface transitorio: visible solo hasta esta deadline (2 s tras el
// primer frame de stream, y mientras tocas + 2 s tras soltar).
static uint64_t g_tp_show_until = 0;
static bool g_tp_stream_seen = false; // ya llego el primer frame de video

// Teclado del sistema (nn::swkbd)
enum class Prompt { None, IP, PsnId, AccountId, Pin, LoginPin };
static FSClient *g_fs_client = nullptr;
static void *g_swkbd_work_mem = nullptr;
static bool g_swkbd_created = false;
static bool g_swkbd_visible = false;
static Prompt g_prompt = Prompt::None;
static Prompt g_swkbd_reopen = Prompt::None;
static char16_t g_swkbd_initial[40];
// true cuando el Account ID se pide DENTRO del flujo de regist (↑):
// ahi guardar encadena al PIN; con SELECT (edicion libre) no.
static bool g_accountid_regist_flow = false;
// Tras DisappearInputForm el swkbd retail necesita Calc unos frames para
// completar su transicion de salida; ver el guard en swkbd_frame.
#define SWKBD_HIDE_FRAMES 30
static uint32_t g_swkbd_hide_frames = 0;
static int g_swkbd_appear_retries = 0;

// ---------------------------------------------------------------------------
// Log a archivo (SD): fs:/vol/external01/wiiu/apps/chiaki/log.txt
// Se registra como handler de WHB: TODO lo que pase por WHBLogWrite/Print/Printf
// (incluido el vuelco de registros y backtrace del crash handler de wut) cae en
// el archivo. Cada linea lleva [s.mmm] desde el arranque. Ademas todo sigue
// saliendo en consola cafe (OSReport) y por UDP 4405.
// ---------------------------------------------------------------------------
#define LOG_SD_ROTATE_BYTES (512 * 1024)

static FSClient *g_log_fs_client = nullptr;
static FSCmdBlock *g_log_fs_cmd = nullptr;
static FSFileHandle g_log_fd = -1;
// Un solo escritor en FS: solo quien retiene g_log_mutex toca el cliente
// y el handle; quien no lo coge, ENCOLA (nunca FS). Motivo: la ruta
// anterior usaba un segundo cliente con g_log_fd (abierto en el cliente
// primario) cuando OSTryLock fallaba, y el retail FSA no admite un handle
// de una sesion de cliente en otra: FSWriteFile se colgaba para siempre,
// el hilo de regist quedaba muerto a medias y el join del apagado se
// colgaba con el — la consola se congelaba al salir al HOME.
static OSMutex g_log_mutex;
static OSMutex g_logq_mutex; // solo head/tail; jamas se bloquea con ella
static bool g_log_ready = false;

// Cola de lineas pendientes de volcar a la SD. El handler SOLO encola
// (microsegundos, jamas FS); la escritura la hace el bucle principal en
// lotes. Motivo: cada FSWriteFile+FSFlushFile cuesta ~4-5 ms fijos en
// la SD y los pagaba el hilo que logueaba — en una tormenta de perdidas
// el hilo de red escribia 8-14 lineas por frame y pasaba 20-70 ms en la
// SD en vez de recoger paquetes (espiral autoinfligida, gui-42).
// Buffers alineados a 64 (mismo criterio que wut usa en su file.c para
// los buffers que cruzan IPC hacia el modulo FS). Estaticos para no
// comer la pila del hilo de 4 KB del crash handler. Los bloqueos del
// camino de encolado son TryLock: el logger jamas se bloquea (un hilo
// muerto con el mutex no puede hundir al resto; si la cola esta
// ocupada, la linea se pierde aqui pero ya esta en UDP/OSReport).
#define LOGQ_LINES 32
#define LOGQ_SIZE 640
static char g_logq[LOGQ_LINES][LOGQ_SIZE] __attribute__((aligned(64)));
static uint32_t g_logq_head = 0; // proxima a vaciar (dueno del mutex)
static uint32_t g_logq_tail = 0; // proxima a encolar (el que encola)

// Lote de vuelco: las lineas del lote se copian aqui contiguas y salen
// en UN FSWriteFile + UN FSFlushFile (coste fijo por llamada).
#define LOG_BATCH_SIZE 0x2000
static char g_log_batch[LOG_BATCH_SIZE] __attribute__((aligned(64)));

// Formatea "[t] str\r\n" (con NUL final); devuelve los bytes utiles.
static int log_line_format(char *dst, size_t dst_size, const char *str)
{
	// El timer real es busClock/4 (~62.5 MHz); el divisor viejo (125000)
	// media el tiempo a la mitad del real (se veía en gui-32/33).
	uint64_t ms = OSTicksToMilliseconds(OSGetTime());
	int n = snprintf(dst, dst_size, "[%llu.%03llu] ",
			 (unsigned long long)(ms / 1000),
			 (unsigned long long)(ms % 1000));
	if(n < 0)
		n = 0;
	// Copia acotada (los vuelcos del crash handler llegan en trozos de
	// 4 KB; aqui se truncan a lo que cabe en la linea).
	size_t len = strlen(str);
	size_t room = dst_size - (size_t)n - 3;
	if(len > room)
		len = room;
	memcpy(dst + n, str, len);
	n += (int)len;
	if(n == 0 || dst[n - 1] != '\n')
	{
		dst[n++] = '\r';
		dst[n++] = '\n';
	}
	dst[n] = '\0';
	return n;
}

// SIEMPRE con g_log_mutex retenido (unico escritor FS). Un FSWriteFile
// por linea (comando atomico en la cola del cliente) + flush: si la app
// muere sin cerrar el archivo, lo escrito ya esta en el soporte.
static void log_line_write(const char *buf, int n)
{
	FSWriteFile(g_log_fs_client, g_log_fs_cmd, (uint8_t *)(uintptr_t)buf,
		    (uint32_t)n, 1, g_log_fd, 0, FS_ERROR_FLAG_ALL);
	FSFlushFile(g_log_fs_client, g_log_fs_cmd, g_log_fd, FS_ERROR_FLAG_ALL);
}

// Extrae lineas de la cola en dst (tantas como quepan, sin romper
// lineas). TryLock de la cola: si alguien esta encolando ahora mismo,
// devuelve 0 y probamos en el siguiente pase. Precondicion: el llamante
// retiene g_log_mutex (unico escritor FS).
static size_t logq_take_batch(char *dst, size_t dst_size)
{
	size_t used = 0;
	if(!OSTryLockMutex(&g_logq_mutex))
		return 0;
	while(g_logq_head != g_logq_tail)
	{
		uint32_t slot = g_logq_head % LOGQ_LINES;
		size_t len = strlen(g_logq[slot]);
		if(used + len > dst_size)
			break; // no cabe mas en este lote
		memcpy(dst + used, g_logq[slot], len);
		used += len;
		g_logq_head++;
	}
	OSUnlockMutex(&g_logq_mutex);
	return used;
}

// Vuelca un lote a la SD (UN escritura + UN flush). La llama el bucle
// principal en cada pasada: TryLocks por todas partes, jamas bloquea.
static void logq_drain_batch(void)
{
	if(g_log_fd < 0)
		return;
	if(!OSTryLockMutex(&g_log_mutex))
		return; // otro drenando ahora; al siguiente pase
	size_t used = logq_take_batch(g_log_batch, sizeof(g_log_batch));
	if(used)
		log_line_write(g_log_batch, (int)used);
	OSUnlockMutex(&g_log_mutex);
}

// Handler de WHB para el log de la SD: SOLO encolar. La escritura FS la
// hace el bucle principal en lotes (logq_drain_batch). Contra: un crash
// duro puede dejar lineas sin volcar a la SD (el vuelco completo sigue
// saliendo por UDP 4405 y por OSReport en la consola cafe).
static void log_sd_handler(const char *str)
{
	if(!g_log_ready)
		return;

	if(!OSTryLockMutex(&g_logq_mutex))
		return; // un microsegundo ocupada: se pierde aqui (UDP ya la tiene)
	char line[LOGQ_SIZE] __attribute__((aligned(64)));
	int n = log_line_format(line, sizeof(line), str);
	if(g_logq_tail - g_logq_head >= LOGQ_LINES)
		g_logq_head++; // llena: descartamos la mas vieja
	memcpy(g_logq[g_logq_tail % LOGQ_LINES], line, (size_t)n + 1);
	g_logq_tail++;
	OSUnlockMutex(&g_logq_mutex);
}

// Dedup de seguridad: una misma linea consecutiva se registra una vez
// y despues solo cada 100 repeticiones con "(xN repetido)". Protege el
// log de la SD si alguna ruta llega a spammear (750 KB vistos en la
// primera prueba de video). Comparacion por hash FNV-1a de 64 bits,
// sin mutex a proposito: es solo diagnostico y un valor torcido en una
// carrera entre hilos como mucho registra la linea igual.
static uint64_t s_log_last_hash;
static uint32_t s_log_repeat;

static uint64_t log_line_hash(const char *s)
{
	uint64_t h = 14695981039346656037ULL;
	for(; *s; s++)
	{
		h ^= (uint8_t)*s;
		h *= 1099511628211ULL;
	}
	return h;
}

static void logprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logprintf(const char *fmt, ...)
{
	char msg[400];
	va_list va;
	va_start(va, fmt);
	vsnprintf(msg, sizeof(msg), fmt, va);
	va_end(va);

	const uint64_t hash = log_line_hash(msg);
	if(hash == s_log_last_hash)
	{
		s_log_repeat++;
		if((s_log_repeat % 100) == 0)
		{
			char extra[430];
			snprintf(extra, sizeof(extra), "%s (x%u repetido)", msg,
				 s_log_repeat);
			WHBLogWrite(extra);
		}
		return;
	}
	s_log_last_hash = hash;
	s_log_repeat = 0;
	WHBLogWrite(msg); // handlers: cafe OSReport + UDP 4405 + SD (handler)
}

// Sink de log del modulo de video (h264/GX2), registrado en main().
static void video_log_sink(const char *line)
{
	logprintf("%s", line);
}

// Requiere FSInit() ya llamado. Si la carpeta no existe, seguimos con
// consola cafe + UDP y lo indicamos una sola vez.
static void log_file_init()
{
	OSInitMutex(&g_log_mutex);
	OSInitMutex(&g_logq_mutex);
	g_log_fs_client = (FSClient *)MEMAllocFromDefaultHeap(sizeof(FSClient));
	g_log_fs_cmd = (FSCmdBlock *)MEMAllocFromDefaultHeapEx(sizeof(FSCmdBlock), 0x1000);
	if(!g_log_fs_client || !g_log_fs_cmd)
	{
		logprintf("Log SD: sin memoria para los clientes FS");
		return;
	}
	// FS_ERROR_FLAG_ALL en TODOS los calls: el FSA retail con
	// FS_ERROR_FLAG_NONE no devuelve los errores como status — enruta el
	// fallo al mecanismo de error del cliente y el call sincrono se queda
	// BLOQUEADO (congelacion en consola; Cemu ignora la mask por completo,
	// por eso ahi nunca fallo). Con ALL los errores llegan como status
	// (FSGetStat de fichero inexistente, FSMakeDir de carpeta ya
	// existente...). Mismo patron que el file.c del propio wut, probado en
	// hardware. La mask de swkbd (cliente del sistema) queda como estaba.
	FSAddClient(g_log_fs_client, FS_ERROR_FLAG_ALL);
	FSInitCmdBlock(g_log_fs_cmd);
	logprintf("Log SD: 1/5 cliente FS listo");

	// Buffers que cruzan al modulo FSA con alineacion 64 (regla del FSA
	// real para buffers de ioctl: en Cemu no se comprueba, en consola si).
	FSStat stat __attribute__((aligned(64)));
	FSMountSource mount_src __attribute__((aligned(64)));
	char mount_path[64] __attribute__((aligned(64)));

	// ¿La SD ya esta montada? En Aroma/HBL el loader la pre-monta y
	// volver a montar puede dejar colgado el call sincrono de FSA en
	// consola. Sondeamos /vol/external01: si responde, no tocamos nada.
	FSStatus vol_st = FSGetStat(g_log_fs_client, g_log_fs_cmd,
				    "/vol/external01", &stat, FS_ERROR_FLAG_ALL);
	FSStatus mount_st = FS_STATUS_OK;
	const char *base = "/vol/external01";
	if(vol_st == FS_STATUS_OK)
	{
		logprintf("Log SD: 2/5 SD ya montada por el loader");
	}
	else
	{
		// Cemu: /vol/external01 no existe hasta que la app lo pide
		// (FSGetMountSource(SD) + FSMount, como hacen los juegos).
		logprintf("Log SD: 2/5 montando la SD (stat vol=%d)", (int)vol_st);
		mount_st = FSGetMountSource(g_log_fs_client, g_log_fs_cmd,
					    FS_MOUNT_SOURCE_SD, &mount_src,
					    FS_ERROR_FLAG_ALL);
		if(mount_st == FS_STATUS_OK)
			mount_st = FSMount(g_log_fs_client, g_log_fs_cmd,
					   &mount_src, mount_path,
					   sizeof(mount_path), FS_ERROR_FLAG_ALL);
		if(mount_st == FS_STATUS_OK)
			base = mount_path;
		logprintf("Log SD: 3/5 montaje=%d", (int)mount_st);
	}

	// Rutas SIN prefijo "fs:": la forma canonica es /vol/external01/...
	// (como en los juegos). Con "fs:" la capa FSA/fsc no resuelve la
	// ruta: mkdir daba FATAL_ERROR y open NOT_FOUND en Cemu.
	char log_dir[128];
	char log_path[160];
	snprintf(log_dir, sizeof(log_dir), "%s/wiiu/apps/chiaki", base);
	snprintf(log_path, sizeof(log_path), "%s/log.txt", log_dir);

	// Carpeta propia, aunque el .wuhb no viva en apps/chiaki. Si ya
	// existe, FSMakeDir devuelve ALREADY_EXISTS (-5), que ignoramos.
	FSStatus mkdir_st = FSMakeDir(g_log_fs_client, g_log_fs_cmd, log_dir,
				      FS_ERROR_FLAG_ALL);
	logprintf("Log SD: 4/5 mkdir=%d", (int)mkdir_st);

	// Rotacion: si el log anterior ya es grande, empezamos de cero.
	if(FSGetStat(g_log_fs_client, g_log_fs_cmd, log_path, &stat,
		     FS_ERROR_FLAG_ALL) == FS_STATUS_OK &&
	   stat.size >= LOG_SD_ROTATE_BYTES)
		FSRemove(g_log_fs_client, g_log_fs_cmd, log_path, FS_ERROR_FLAG_ALL);

	// "a" (append); si el entorno no lo soporta, "w" crea/trunca.
	FSStatus open_st = FSOpenFile(g_log_fs_client, g_log_fs_cmd, log_path,
				       "a", &g_log_fd, FS_ERROR_FLAG_ALL);
	if(open_st != FS_STATUS_OK)
		open_st = FSOpenFile(g_log_fs_client, g_log_fs_cmd, log_path,
				     "w", &g_log_fd, FS_ERROR_FLAG_ALL);
	logprintf("Log SD: 5/5 open=%d", (int)open_st);
	if(open_st != FS_STATUS_OK)
	{
		g_log_fd = -1;
		logprintf("Log SD: no se pudo abrir %s (mount=%d mkdir=%d open=%d)",
			    log_path, (int)mount_st, (int)mkdir_st, (int)open_st);
		return;
	}
	g_log_ready = true;
	WHBAddLogHandler(log_sd_handler);
	logprintf("Log SD: escribiendo en %s", log_path);
}

static void log_file_close()
{
	WHBRemoveLogHandler(log_sd_handler);
	g_log_ready = false;
	// Unico escritor FS: ahora podemos bloquear con tranquilidad. Vacia
	// la cola pendiente por lotes antes de cerrar.
	OSLockMutex(&g_log_mutex);
	for(;;)
	{
		size_t used = logq_take_batch(g_log_batch, sizeof(g_log_batch));
		if(!used)
			break;
		log_line_write(g_log_batch, (int)used);
	}
	if(g_log_fd >= 0)
	{
		FSFlushFile(g_log_fs_client, g_log_fs_cmd, g_log_fd, FS_ERROR_FLAG_ALL);
		FSCloseFile(g_log_fs_client, g_log_fs_cmd, g_log_fd, FS_ERROR_FLAG_ALL);
		g_log_fd = -1;
	}
	OSUnlockMutex(&g_log_mutex);
	if(g_log_fs_client)
	{
		FSDelClient(g_log_fs_client, FS_ERROR_FLAG_ALL);
		MEMFreeToDefaultHeap(g_log_fs_client);
		MEMFreeToDefaultHeap(g_log_fs_cmd);
		g_log_fs_client = nullptr;
		g_log_fs_cmd = nullptr;
	}
}

static void log_vpad_changes(const VPADStatus &status, uint32_t &last_hold,
			     VPADTouchData &last_touch)
{
	if(status.hold != last_hold)
	{
		logprintf("VPAD hold: %08x", status.hold);
		last_hold = status.hold;
	}

	const VPADTouchData &touch = status.tpFiltered2;
	if(touch.touched != last_touch.touched ||
	   (touch.touched && (touch.x != last_touch.x || touch.y != last_touch.y)))
	{
		if(touch.touched)
			logprintf("VPAD touch: %u,%u", (unsigned)touch.x, (unsigned)touch.y);
		else
			logprintf("VPAD touch: released");
		last_touch = touch;
	}
}

// Puente chiaki-log → WHBLog: todo lo que loguea chiaki-lib aparece en la
// consola cafe (OSReport, visible en el log de Cemu) y en el UDP 4405.
//
// Rate-limit para INFO/WARNING: si el stream se rompe, chiaki avienta
// cientos de lineas por segundo ("corrupt frame" con rangos de indices
// distintos, imposibles de deduplicar) y el log se dispara. Tope de
// 20 lineas/s para W/I; las suprimidas se resume en una sola linea al
// cerrar la ventana. ERROR siempre pasa (pocos y determinantes).
static void wiiu_chiaki_log_cb(ChiakiLogLevel level, const char *msg, void *user)
{
	(void)user;
	if(level == CHIAKI_LOG_ERROR)
	{
		logprintf("[chiaki:%c] %s", chiaki_log_level_char(level), msg);
		return;
	}

	// Best-effort sin lock (como el dedup de logprintf): en el peor caso
	// un hilo deja pasar unas lineas de mas, nunca bloquea el hilo chiaki.
	static uint64_t s_window_start;
	static uint32_t s_budget;
	static uint32_t s_suppressed;
	uint64_t now = OSGetTime();
	if(now - s_window_start > OSSecondsToTicks(1))
	{
		if(s_suppressed)
			logprintf("[chiaki:*] (rate limit: %u lineas de aviso/info suprimidas)",
				  s_suppressed);
		s_suppressed = 0;
		s_window_start = now;
		s_budget = 20;
	}
	if(s_budget == 0)
	{
		s_suppressed++;
		return;
	}
	s_budget--;
	logprintf("[chiaki:%c] %s", chiaki_log_level_char(level), msg);
}

// cb del ChiakiDiscoveryService: se invoca (en el thread del servicio) cada vez
// que cambia la lista de consolas conocidas (alta, cambio de estado o drop).
// Ademas de loguear, deja un snapshot de la consola usable por ↑/A.
static void discovery_state_cb(ChiakiDiscoveryHost *hosts, size_t hosts_count, void *user)
{
	ChiakiLog *log = (ChiakiLog *)user;
	CHIAKI_LOGI(log, "Discovery state: %zu host(s)", hosts_count);
	for(size_t i = 0; i < hosts_count; i++)
	{
		ChiakiDiscoveryHost *host = hosts + i;
		CHIAKI_LOGI(log, "  host[%u]: %s (%s) state=%s req_port=%u",
			    (unsigned int)i,
			    host->host_name ? host->host_name : "?",
			    host->host_addr ? host->host_addr : "?",
			    chiaki_discovery_host_state_string(host->state),
			    (unsigned int)host->host_request_port);
		if(host->running_app_name && host->running_app_name[0])
			CHIAKI_LOGI(log, "    running: %s (%s)", host->running_app_name,
				    host->running_app_titleid ? host->running_app_titleid : "?");
	}

	// Preferimos una consola READY; si no, la primera conocida.
	ChiakiDiscoveryHost *pick = nullptr;
	for(size_t i = 0; i < hosts_count; i++)
	{
		if(hosts[i].state == CHIAKI_DISCOVERY_HOST_STATE_READY)
		{
			pick = hosts + i;
			break;
		}
	}
	if(!pick && hosts_count)
		pick = hosts;

	chiaki_mutex_lock(&g_host_mutex);
	if(pick && pick->host_addr)
	{
		snprintf(g_host_addr, sizeof(g_host_addr), "%s", pick->host_addr);
		snprintf(g_host_nickname, sizeof(g_host_nickname), "%s",
			 pick->host_name ? pick->host_name : "");
		g_host_request_port = pick->host_request_port;
		g_host_state = pick->state;
		g_host_target = chiaki_discovery_host_system_version_target(pick);
		g_host_found = true;
	}
	else
	{
		g_host_found = false;
		g_host_state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
	}
	chiaki_mutex_unlock(&g_host_mutex);
}

static bool start_discovery(const char *host)
{
	ChiakiDiscoveryServiceOptions options = {};
	options.ping_ms = DISCOVERY_PING_MS;
	options.ping_initial_ms = DISCOVERY_PING_MS;
	options.hosts_max = DISCOVERY_HOSTS_MAX;
	options.host_drop_pings = DISCOVERY_DROP_PINGS;
	options.cb = discovery_state_cb;
	options.cb_user = &g_chiaki_log;

	struct sockaddr_in send_addr = {};
	send_addr.sin_family = AF_INET;
	if(host && host[0])
	{
		// Discovery dirigido: el servicio resuelve send_host con
		// getaddrinfo en el primer ping y sustituye send_addr.
		// Evita el broadcast, que Cemu no soporta (ignora SO_BROADCAST).
		options.send_host = (char *)host;
		send_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else
	{
		// Broadcast limitado 255.255.255.255 (PS4 :987 / PS5 :9302).
		send_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	}

	struct sockaddr_storage send_storage = {};
	memcpy(&send_storage, &send_addr, sizeof(send_addr));
	options.send_addr = &send_storage;
	options.send_addr_size = sizeof(send_addr);
	options.broadcast_addrs = nullptr;
	options.broadcast_num = 0;

	ChiakiErrorCode err = chiaki_discovery_service_init(&g_discovery_service,
							 &options, &g_chiaki_log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(&g_chiaki_log, "Discovery service init failed: %s",
			    chiaki_error_string(err));
		return false;
	}

	if(host && host[0])
		CHIAKI_LOGI(&g_chiaki_log, "Discovery started -> IP dirigida %s (PS4 :987 / PS5 :9302)", host);
	else
		CHIAKI_LOGI(&g_chiaki_log, "Discovery started -> broadcast 255.255.255.255 (ping cada %d ms)",
			    DISCOVERY_PING_MS);
	return true;
}

static void stop_discovery()
{
	if(!g_discovery_running)
		return;
	// Join de sus threads: valida el stoppipe UDP loopback de Cafe OS.
	chiaki_discovery_service_fini(&g_discovery_service);
	g_discovery_running = false;
	logprintf("discovery service stopped");
}

static void restart_discovery()
{
	stop_discovery();
	g_discovery_running = start_discovery(g_host_ip[0] ? g_host_ip : nullptr);
}

static void u16_from_ascii(char16_t *dst, size_t dst_size, const char *src)
{
	size_t i = 0;
	for(; src[i] && i + 1 < dst_size; i++)
		dst[i] = (char16_t)(unsigned char)src[i];
	dst[i] = 0;
}

static bool valid_ipv4(const char *s)
{
	unsigned int a, b, c, d;
	char extra;
	int n = sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra);
	if(n != 4)
		return false;
	return a <= 255 && b <= 255 && c <= 255 && d <= 255;
}

static bool swkbd_init()
{
	g_fs_client = (FSClient *)MEMAllocFromDefaultHeap(sizeof(FSClient));
	if(!g_fs_client)
	{
		logprintf("swkbd: alloc FSClient failed");
		return false;
	}
	FSAddClient(g_fs_client, FS_ERROR_FLAG_NONE);

	g_swkbd_work_mem = MEMAllocFromDefaultHeap(nn::swkbd::GetWorkMemorySize(0));
	if(!g_swkbd_work_mem)
	{
		logprintf("swkbd: alloc work memory failed");
		return false;
	}

	nn::swkbd::CreateArg createArg;
	createArg.regionType = nn::swkbd::RegionType::USA;
	createArg.workMemory = g_swkbd_work_mem;
	createArg.fsClient = g_fs_client;
	if(!nn::swkbd::Create(createArg))
	{
		logprintf("swkbd: Create failed");
		return false;
	}
	g_swkbd_created = true;
	nn::swkbd::MuteAllSound(false);
	return true;
}

// Muestra el formulario de entrada adecuado al proposito actual.
static void swkbd_show(Prompt p)
{
	if(!g_swkbd_created || g_swkbd_visible)
		return;

	nn::swkbd::AppearArg appearArg = {};
	appearArg.keyboardArg.configArg.languageType = nn::swkbd::LanguageType::Spanish;
	appearArg.inputFormArg.maxTextLength = 15;

	switch(p)
	{
	case Prompt::IP:
		appearArg.inputFormArg.hintText = u"IP de la consola (ej. 192.168.1.10)";
		if(g_host_ip[0])
		{
			u16_from_ascii(g_swkbd_initial,
				       sizeof(g_swkbd_initial) / sizeof(char16_t), g_host_ip);
			appearArg.inputFormArg.initialText = g_swkbd_initial;
		}
		break;
	case Prompt::PsnId:
		appearArg.inputFormArg.hintText = u"Tu ID publico de PSN (nombre de usuario)";
		appearArg.inputFormArg.maxTextLength = 32;
		if(g_psn_online_id[0])
		{
			u16_from_ascii(g_swkbd_initial,
				       sizeof(g_swkbd_initial) / sizeof(char16_t), g_psn_online_id);
			appearArg.inputFormArg.initialText = g_swkbd_initial;
		}
		break;
	case Prompt::AccountId:
		// Formato unico en el UI: base64 (el que sirve la API de
		// PSN, para copiar y pegar directo de psn.flipscreen.games).
		appearArg.inputFormArg.hintText = u"Account ID PSN en base64 (ej. wLEMeFoyimY=)";
		appearArg.inputFormArg.maxTextLength = 16;
		// Prefill con el ID actual si ya hay uno (SELECT = editar):
		// asi solo se corrigen los digitos que fallan.
		if(g_have_psn_account)
		{
			char b64[13] = { 0 };
			if(chiaki_base64_encode(g_psn_account_id,
					      sizeof(g_psn_account_id), b64,
					      sizeof(b64)) == CHIAKI_ERR_SUCCESS)
			{
				u16_from_ascii(g_swkbd_initial,
					       sizeof(g_swkbd_initial) / sizeof(char16_t),
					       b64);
				appearArg.inputFormArg.initialText = g_swkbd_initial;
			}
		}
		break;
	case Prompt::Pin:
		appearArg.inputFormArg.hintText = u"PIN de 8 digitos mostrado en la PS4";
		appearArg.inputFormArg.maxTextLength = 8;
		break;
	case Prompt::LoginPin:
		appearArg.inputFormArg.hintText = u"PIN de acceso configurado en la PS4";
		appearArg.inputFormArg.maxTextLength = 16;
		break;
	default:
		return;
	}

	if(!nn::swkbd::AppearInputForm(appearArg))
	{
		g_swkbd_appear_retries++;
		logprintf("swkbd: AppearInputForm fallo (intento %d/3)%s",
			     g_swkbd_appear_retries,
			     g_swkbd_appear_retries < 3 ?
				     " — reintento" :
				     " — teclado abandonado");
		if(g_swkbd_appear_retries < 3)
			g_swkbd_reopen = p;
		return;
	}
	g_swkbd_appear_retries = 0;
	g_prompt = p;
	g_swkbd_visible = true;
	logprintf("Teclado abierto (A=OK, B=cancelar)");
}

// Cuenta PSN: se introduce a mano. La consulta HTTP a flipscreen.games se
// descarto tras un crash de Cemu en la ruta TLS de curl; ademas no todo el
// mundo tiene ID online publica. Acepta el account id en hex (16 digitos, el
// formato habitual) o en base64 (lo que sirve la API por si lo copias de ahi).
static int hex_digit(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool parse_psn_account_id(const char *s, uint8_t out[CHIAKI_PSN_ACCOUNT_ID_SIZE])
{
	size_t len = strlen(s);
	if(len == CHIAKI_PSN_ACCOUNT_ID_SIZE * 2)
	{
		bool hex = true;
		for(size_t i = 0; hex && i < len; i++)
			hex = hex_digit(s[i]) >= 0;
		if(hex)
		{
			for(size_t i = 0; i < CHIAKI_PSN_ACCOUNT_ID_SIZE; i++)
				out[i] = (uint8_t)((hex_digit(s[i * 2]) << 4) |
						   hex_digit(s[i * 2 + 1]));
			return true;
		}
	}
	size_t out_size = CHIAKI_PSN_ACCOUNT_ID_SIZE;
	if(chiaki_base64_decode(s, len, out, &out_size) == CHIAKI_ERR_SUCCESS &&
	   out_size == CHIAKI_PSN_ACCOUNT_ID_SIZE)
		return true;
	return false;
}

// newlib declara basename() en <string.h> pero la renombra a __gnu_basename
// (ese sí existe en libc); la config cross de curl marcó HAVE_BASENAME y
// mime.o pide el simbolo plano `basename`, ausente en libc. Lo aportamos con
// semantica POSIX (la documentada en curl/lib/mime.c): modifica path in-place
// y devuelve el ultimo componente ("/" si todo son barras, "." si vacio).
// La etiqueta asm evita el choque con la declaracion renombrada de string.h.
extern "C" char *wiiu_posix_basename(const char *path) asm("basename");
extern "C" char *wiiu_posix_basename(const char *path)
{
	if(!path || !*path)
		return (char *)".";
	char *p = const_cast<char *>(path);
	size_t len = strlen(p);
	while(len > 1 && p[len - 1] == '/')
		p[--len] = 0;
	if(len == 1 && p[0] == '/')
		return p;
	char *slash = strrchr(p, '/');
	return slash ? slash + 1 : p;
}

// ---------------------------------------------------------------------------
// Persistencia (fase 4a): sd:/wiiu/apps/chiaki/config.ini por el mismo
// cliente FS que el log, siempre con g_log_mutex (unico escritor FS en la
// app; el logger usa TryLock y encola si estamos escribiendo aqui).
// ---------------------------------------------------------------------------
#define CONFIG_SD_PATH "/vol/external01/wiiu/apps/chiaki/config.ini"

static int sd_read_file(const char *path, char *buf, size_t cap)
{
	if(!g_log_ready || cap < 2)
		return -1;
	OSLockMutex(&g_log_mutex);
	FSFileHandle fd = -1;
	FSStatus st = FSOpenFile(g_log_fs_client, g_log_fs_cmd, path, "r",
				 &fd, FS_ERROR_FLAG_ALL);
	int total = 0;
	if(st >= 0)
	{
		// Patron WHB (whb/file.c): FSGetStatFile + UNA FSReadFile con
		// el tamano exacto del fichero, en vez de un bucle con count
		// mayor que los bytes disponibles.
		FSStat stat;
		FSStatus ss = FSGetStatFile(g_log_fs_client, g_log_fs_cmd, fd,
					    &stat, FS_ERROR_FLAG_ALL);
		if(ss >= 0 && stat.size > 0)
		{
			uint32_t want = stat.size;
			if(want > (uint32_t)(cap - 1))
				want = (uint32_t)(cap - 1);
			FSStatus rs = FSReadFile(g_log_fs_client, g_log_fs_cmd,
						 (uint8_t *)(uintptr_t)buf, 1,
						 want, fd, 0, FS_ERROR_FLAG_ALL);
			if(rs > 0)
				total = (int)rs;
		}
		FSCloseFile(g_log_fs_client, g_log_fs_cmd, fd,
			    FS_ERROR_FLAG_ALL);
	}
	buf[total] = '\0';
	OSUnlockMutex(&g_log_mutex);
	return st >= 0 ? total : -1;
}

static bool sd_write_file(const char *path, const char *buf, size_t len)
{
	if(!g_log_ready)
		return false;
	OSLockMutex(&g_log_mutex);
	FSFileHandle fd = -1;
	FSStatus st = FSOpenFile(g_log_fs_client, g_log_fs_cmd, path, "w",
				 &fd, FS_ERROR_FLAG_ALL);
	bool ok = false;
	if(st >= 0)
	{
		FSStatus ws = FSWriteFile(g_log_fs_client, g_log_fs_cmd,
					   (uint8_t *)(uintptr_t)buf,
					   (uint32_t)len, 1, fd, 0,
					   FS_ERROR_FLAG_ALL);
		FSFlushFile(g_log_fs_client, g_log_fs_cmd, fd, FS_ERROR_FLAG_ALL);
		FSCloseFile(g_log_fs_client, g_log_fs_cmd, fd, FS_ERROR_FLAG_ALL);
		ok = ws >= 0;
	}
	OSUnlockMutex(&g_log_mutex);
	return ok;
}

static char g_cfg_in[4096] __attribute__((aligned(64)));
static char g_cfg_out[2048] __attribute__((aligned(64)));

// Aplica la consola idx a los globals de sesion (IP + claves + nombre)
// y la marca como seleccionada en el menu. Asi A conecta siempre con la
// consola resaltada, igual al arranque o al cambiar con Izq/Dcha.
static bool apply_console(int idx)
{
	if(idx < 0 || idx >= config_console_count(&g_config))
		return false;
	const ConfigConsole *c = &g_config.consoles[idx];
	snprintf(g_host_ip, sizeof(g_host_ip), "%s", c->host);
	memcpy(g_regist_key, c->regist_key, sizeof(g_regist_key));
	memcpy(g_morning, c->morning, sizeof(g_morning));
	if(c->name[0])
		snprintf(g_host_nickname, sizeof(g_host_nickname), "%s", c->name);
	g_registered = true;
	g_menu_sel = idx;
	return true;
}

static void config_load_sd(void)
{
	int n = sd_read_file(CONFIG_SD_PATH, g_cfg_in, sizeof(g_cfg_in));
	if(n <= 0)
	{
		logprintf("Config: sin %s todavia (se creara al registrar)",
			  CONFIG_SD_PATH);
		return;
	}
	config_parse(&g_config, g_cfg_in, (size_t)n);
	int count = config_console_count(&g_config);
	logprintf("Config: %d consola(s) guardadas, account_id=%s", count,
		  g_config.account_id_hex[0] ? g_config.account_id_hex
					     : "(ninguna)");

	// Preseleccionar la ultima consola (o la primera): IP + claves +
	// cuenta => A conecta sin repetir registro ni teclados.
	const char *host = g_config.last_host[0]
				   ? g_config.last_host
				   : (count > 0 ? g_config.consoles[0].host : nullptr);
	int idx = config_find(&g_config, host);
	if(idx >= 0 && apply_console(idx))
		logprintf("Config: lista '%s' (%s) — A conecta sin registrar de nuevo",
			  g_config.consoles[idx].name[0]
				  ? g_config.consoles[idx].name
				  : "?",
			  g_config.consoles[idx].host);
	if(g_config.account_id_hex[0] &&
	   parse_psn_account_id(g_config.account_id_hex, g_psn_account_id))
		g_have_psn_account = true;
}

// Serializa bajo g_host_mutex (el cb de regist escribe desde su thread) y
// escribe el fichero fuera del mutex.
static void config_save_sd(void)
{
	chiaki_mutex_lock(&g_host_mutex);
	size_t len = config_serialize(&g_config, g_cfg_out, sizeof(g_cfg_out));
	chiaki_mutex_unlock(&g_host_mutex);
	if(!len)
		return;
	if(sd_write_file(CONFIG_SD_PATH, g_cfg_out, len))
		logprintf("Config: guardada en %s (%u bytes)", CONFIG_SD_PATH,
			  (unsigned int)len);
	else
		logprintf("Config: fallo al escribir %s", CONFIG_SD_PATH);
}

// Borra la consola seleccionada, persiste en SD y re-aplica la nueva
// seleccion (IP + claves) para que A siga conectando a lo que quede.
// El snapshot de discovery puede ser de la consola borrada: se limpia
// y se reinicia en broadcast (asi el estado vacio vuelve a buscar).
static void delete_selected_console(void)
{
	int idx = g_menu_sel;
	int count = config_console_count(&g_config);
	if(count <= 0 || idx < 0 || idx >= count)
	{
		// Nunca un retorno silencioso: si la seleccion no cuadra se
		// ve en el pie y en el log (asi un "no se borro" es rastreable).
		logprintf("Borrado: seleccion invalida (idx=%d, count=%d)",
			  idx, count);
		snprintf(g_last_session_msg, sizeof(g_last_session_msg),
			 "No se pudo eliminar la consola");
		return;
	}

	char host[CONFIG_HOST_SIZE];
	char name[CONFIG_NAME_SIZE + 1];
	snprintf(host, sizeof(host), "%s", g_config.consoles[idx].host);
	snprintf(name, sizeof(name), "%s", g_config.consoles[idx].name);

	if(!config_del_console(&g_config, idx))
	{
		logprintf("Borrado: config_del_console fallo (idx=%d)", idx);
		snprintf(g_last_session_msg, sizeof(g_last_session_msg),
			 "No se pudo eliminar la consola");
		return;
	}
	config_save_sd();

	count = config_console_count(&g_config);
	if(g_menu_sel >= count)
		g_menu_sel = count > 0 ? count - 1 : 0;
	if(count > 0)
		apply_console(g_menu_sel);
	else
	{
		// Lista vacia: nada de la consola borrada queda vivo —
		// ni cuenta, ni claves, ni IP de sesion.
		g_registered = false;
		g_host_ip[0] = 0;
		g_host_nickname[0] = 0;
		memset(g_regist_key, 0, sizeof(g_regist_key));
		memset(g_morning, 0, sizeof(g_morning));
	}

	chiaki_mutex_lock(&g_host_mutex);
	g_host_found = false;
	g_host_state = CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
	g_host_addr[0] = 0;
	chiaki_mutex_unlock(&g_host_mutex);
	restart_discovery();

	logprintf("Consola eliminada: '%s' (%s) — quedan %d",
		  name[0] ? name : "?", host, count);
	snprintf(g_last_session_msg, sizeof(g_last_session_msg),
		 "Consola eliminada: %s", name[0] ? name : host);
}

// ---------------------------------------------------------------------------
// Registro (chiaki_regist): emparejar con la PS4 mediante el PIN que muestra
// la consola en "Enlazar con otro dispositivo".
// ---------------------------------------------------------------------------

static void regist_event_cb(ChiakiRegistEvent *event, void *user)
{
	(void)user;
	switch(event->type)
	{
	case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
	{
		ChiakiRegisteredHost *rh = event->registered_host;
		chiaki_mutex_lock(&g_host_mutex);
		if(rh)
		{
			memcpy(g_regist_key, rh->rp_regist_key, CHIAKI_SESSION_AUTH_SIZE);
			memcpy(g_morning, rh->rp_key, sizeof(g_morning));
			if(rh->server_nickname[0])
				snprintf(g_host_nickname, sizeof(g_host_nickname), "%s",
					 rh->server_nickname);
			g_registered = true;
			// Fase 4a: persistir ya — el proximo arranque conecta
			// sin repetir registro ni PIN.
			config_set_console(&g_config, g_regist_target_host,
					   rh->server_nickname,
					   (const uint8_t *)rh->rp_regist_key,
					   rh->rp_key, g_psn_account_id);
			snprintf(g_config.last_host, sizeof(g_config.last_host),
				 "%s", g_regist_target_host);
			g_config_dirty = true;
		}
		chiaki_mutex_unlock(&g_host_mutex);
		if(rh)
			logprintf("REGISTRO OK: consola '%s' clave=%.16s", rh->server_nickname,
				     rh->rp_regist_key);
		logprintf("Registro guardado en config.ini — pulsa A para conectar Remote Play");
		break;
	}
	case CHIAKI_REGIST_EVENT_TYPE_FINISHED_FAILED:
		logprintf("Registro FALLIDO — revisa el PIN y que la PS4 este en "
			    "'Enlazar con otro dispositivo' (log chiaki arriba)");
		break;
	case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED:
		logprintf("Registro cancelado");
		break;
	}
	// El main llama a chiaki_regist_fini (join) cuando ve este estado.
	g_regist_state = RegistState::Finished;
}

static bool start_regist(uint32_t pin)
{
	if(!g_host_found && !g_host_ip[0])
	{
		logprintf("Regist: sin consola descubierta — X para fijar la IP");
		return false;
	}
	if(g_regist_state != RegistState::Idle)
	{
		logprintf("Regist: ya hay un registro en curso");
		return false;
	}

	chiaki_mutex_lock(&g_host_mutex);
	ChiakiTarget target = g_host_target;
	char host[48];
	snprintf(host, sizeof(host), "%s", g_host_addr);
	chiaki_mutex_unlock(&g_host_mutex);
	if(!g_host_found)
	{
		// Fallback: IP manual tecleada con X (sin snapshot del discovery).
		target = CHIAKI_TARGET_PS4_10;
		snprintf(host, sizeof(host), "%s", g_host_ip);
		logprintf("Regist: usando IP manual %s (PS4 10.x)", host);
	}
	// Fase 4a: el cb de regist usa este host para guardar la consola.
	snprintf(g_regist_target_host, sizeof(g_regist_target_host), "%s", host);

	if(target == CHIAKI_TARGET_PS4_UNKNOWN || target == CHIAKI_TARGET_PS5_UNKNOWN)
	{
		logprintf("Regist: version de la consola desconocida");
		return false;
	}
	if(target >= CHIAKI_TARGET_PS4_9 && !g_have_psn_account)
	{
		logprintf("Regist: falta la cuenta PSN (ID publico)");
		return false;
	}

	ChiakiRegistInfo info = {};
	info.target = target;
	info.broadcast = false;
	info.pin = pin;
	info.console_pin = 0;
	info.holepunch_info = nullptr;
	// Buffer propio: regist guarda el puntero y lo usa en su thread.
	static char regist_host[48];
	snprintf(regist_host, sizeof(regist_host), "%s", host);
	info.host = regist_host;
	if(target >= CHIAKI_TARGET_PS4_9)
	{
		// PS4 >= 7.0: cuenta PSN (8 bytes) en lugar del ID online.
		memcpy(info.psn_account_id, g_psn_account_id, sizeof(info.psn_account_id));
		info.psn_online_id = nullptr;
	}
	else
	{
		info.psn_online_id = g_psn_online_id;
	}

	ChiakiErrorCode err = chiaki_regist_start(&g_regist, &g_chiaki_log, &info,
						   regist_event_cb, nullptr);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		logprintf("Regist: chiaki_regist_start fallo: %s", chiaki_error_string(err));
		return false;
	}
	g_regist_state = RegistState::Running;
	logprintf("Registro iniciado contra %s (PIN en la pantalla de la PS4)", regist_host);
	return true;
}

// ---------------------------------------------------------------------------
// Sesion de Remote Play (chiaki_session)
// ---------------------------------------------------------------------------

// Sin decoder (fase 2): devolvemos true = "decodificado" para no disparar el
// bucle de IDR; contamos los bytes para validar FEC/gf-complete con datos
// reales del stream.
static bool video_sample_cb(uint8_t *buf, size_t buf_size, int32_t frames_lost,
			    bool frame_recovered, void *user)
{
	(void)user;
	(void)frame_recovered;
	g_video_frames++;
	g_video_bytes += buf_size;
	if(frames_lost > 0)
		g_video_frames_lost += (uint64_t)frames_lost;
	// Fase 3a: primera muestra = cabecera SPS/PPS (abre el decoder de
	// hardware y fija las dimensiones); despues, cada frame.
	wiiu_video_sample(buf, buf_size);
	return true;
}

static void session_event_cb(ChiakiEvent *event, void *user)
{
	(void)user;
	switch(event->type)
	{
	case CHIAKI_EVENT_CONNECTED:
		logprintf("Sesion: conectada (session request OK)");
		break;
	case CHIAKI_EVENT_NICKNAME_RECEIVED:
		logprintf("Sesion: PS4 '%s'", event->server_nickname);
		break;
	case CHIAKI_EVENT_LOGIN_PIN_REQUEST:
		g_session_login_pin_incorrect = event->login_pin_request.pin_incorrect;
		g_session_login_pin_requested = true;
		logprintf("Sesion: la PS4 pide PIN de acceso — introducelo con A");
		break;
	case CHIAKI_EVENT_VIDEO_FEC_FAILURE:
		g_fec_failures++;
		logprintf("Sesion: FEC — frame %d perdido (idr enviado=%d)",
			     event->video_fec_failure.frame_index,
			     (int)event->video_fec_failure.idr_request_sent);
		break;
	case CHIAKI_EVENT_QUIT:
		if(event->quit.reason_str)
			logprintf("Sesion terminada: %s (%s)",
				     chiaki_quit_reason_string(event->quit.reason),
				     event->quit.reason_str);
		else
			logprintf("Sesion terminada: %s",
				     chiaki_quit_reason_string(event->quit.reason));
		// Resultado visible en el pie del menu al volver (chiaki
		// solo lo mandaba al log: si la sesion falla, el usuario
		// no veia nada en pantalla).
		if(event->quit.reason == CHIAKI_QUIT_REASON_STOPPED)
			snprintf(g_last_session_msg, sizeof(g_last_session_msg),
				 "Última sesión: detenida");
		else if(event->quit.reason_str)
			snprintf(g_last_session_msg, sizeof(g_last_session_msg),
				 "Última sesión fallida: %s — %s",
				 chiaki_quit_reason_string(event->quit.reason),
				 event->quit.reason_str);
		else
			snprintf(g_last_session_msg, sizeof(g_last_session_msg),
				 "Última sesión fallida: %s",
				 chiaki_quit_reason_string(event->quit.reason));
		g_session_quit_received = true;
		break;
	default:
		break;
	}
}

// Adaptador: logprintf es varargs y audio_set_log recibe texto ya
// formateado (los mensajes de audio ya traen su prefijo "Audio:").
static void audio_log_shim(const char *msg)
{
	logprintf("%s", msg);
}

// Sink de audio (fase 3b): el sink de chiaki entrega OPUS CRUDO (ver
// audioreceiver.h: "Sink that receives Audio encoded as Opus"), así
// que entre él y el ring AX va el decoder opus de la propia librería:
// su callback de salida recibe PCM s16 y lo empujamos al ring.
// Gui-36 empujaba el opus crudo como PCM => sonido rasposo.
static ChiakiOpusDecoder g_opus_decoder;
static uint32_t g_opus_channels = 2;

static void session_opus_settings_cb(uint32_t channels, uint32_t rate,
				     void *user)
{
	(void)user;
	g_opus_channels = channels;
	logprintf("Audio: opus decoder listo (ch=%u rate=%u)", channels,
		  rate);
}

static void session_opus_pcm_cb(int16_t *buf, size_t samples_count,
				void *user)
{
	(void)user;
	// samples_count = muestras por canal; el ring AX quiere PCM
	// entrelazado: bytes = muestras × canales × 2.
	audio_push((const uint8_t *)buf,
		   samples_count * g_opus_channels * sizeof(int16_t));
}

// Presets de calidad de video (fase 4c). La palanca de robustez sobre
// el enlace 2,4 GHz de la Wii U es el bitrate: en la prueba de hw a
// 5 Mbps la sesion fue perfecta (cero frames perdidos en 50 s) y a
// 10 Mbps las ráfagas de interferencia mataban el stream entero. La
// filosofia (igual que Moonlight Wii U): partir bajo y subir hasta
// que deje de fallar — el ladder cubre 5M..1.5M ademas de 540p, que
// es GRATIS aqui (el decoder HW reabre ante cada SPS y las texturas
// GX2 reallocan solas) y da cuadros mas ligeros de subir.
// Se eligen con ZL/ZR en el menu, se guardan en el config y se
// aplican al conectar.
struct VideoPreset
{
	bool fps60;
	uint16_t width, height;
	uint16_t bitrate_kbps;
	const char *label;
};
static const VideoPreset g_video_presets[] = {
	{ true, 1280, 720, 5000, "720p60 5M" },  // 0: validado en hw
	{ true, 1280, 720, 4000, "720p60 4M" },
	{ true, 1280, 720, 3000, "720p60 3M" },
	{ true, 1280, 720, 2500, "720p60 2.5M" },
	{ true, 1280, 720, 2000, "720p60 2M" },
	{ true, 1280, 720, 1500, "720p60 1.5M" }, // punto de partida Moonlight
	{ true, 960, 540, 5000, "540p60 5M" },   // nitidez maxima a 480p fisica
	{ true, 960, 540, 3000, "540p60 3M" },   // cuadro mas ligero
	{ true, 960, 540, 2000, "540p60 2M" },   // red de emergencia
};
#define VIDEO_PRESET_COUNT \
	((int)(sizeof(g_video_presets) / sizeof(g_video_presets[0])))

// Indice saneado: el config puede traer basura de una version vieja.
static int video_preset_cur(void)
{
	int idx = g_config.video_preset;
	return (idx >= 0 && idx < VIDEO_PRESET_COUNT) ? idx : 0;
}

static const char *video_preset_label(int idx)
{
	return g_video_presets[(idx >= 0 && idx < VIDEO_PRESET_COUNT)
				       ? idx
				       : 0]
		.label;
}

static bool start_session()
{
	if(!g_host_found && !g_host_ip[0])
	{
		logprintf("Sesion: sin consola descubierta — X para fijar la IP");
		return false;
	}
	if(!g_registered)
	{
		logprintf("Sesion: sin registro — pulsa ↑ primero (PIN de la PS4)");
		return false;
	}
	if(g_session_active)
	{
		logprintf("Sesion: ya hay una sesion activa (B para parar)");
		return false;
	}

	chiaki_mutex_lock(&g_host_mutex);
	char host[48];
	snprintf(host, sizeof(host), "%s", g_host_addr);
	uint16_t req_port = g_host_request_port;
	chiaki_mutex_unlock(&g_host_mutex);
	if(!g_host_found)
	{
		snprintf(host, sizeof(host), "%s", g_host_ip);
		logprintf("Sesion: usando IP manual %s (sin snapshot del discovery)",
			     host);
	}

	ChiakiConnectInfo info = {};
	info.ps5 = false; // session.c deriva CHIAKI_TARGET_PS4_10 y lo refina
	info.host = host;
	memcpy(info.regist_key, g_regist_key, sizeof(info.regist_key));
	memcpy(info.morning, g_morning, sizeof(info.morning));
	{
		const VideoPreset *vp = &g_video_presets[video_preset_cur()];
		// Asignacion directa del perfil (es lo que hace chiaki por
		// dentro con su preset, pero parametrico para poder llevar
		// 540p): el pipeline se adapta solo — el decoder HW reabre
		// ante cada SPS nuevo y las texturas GX2 reallocan.
		info.video_profile.width = vp->width;
		info.video_profile.height = vp->height;
		info.video_profile.max_fps = vp->fps60 ? 60 : 30;
		info.video_profile.bitrate = vp->bitrate_kbps;
		info.video_profile.codec = CHIAKI_CODEC_H264;
		logprintf("Sesion: perfil video %ux%u@%u %u kbps (%s)",
			  info.video_profile.width,
			  info.video_profile.height,
			  info.video_profile.max_fps,
			  info.video_profile.bitrate, vp->label);
	}
	info.video_profile_auto_downgrade = true;
	info.enable_keyboard = false;
	info.enable_dualsense = false;
	info.audio_video_disabled = CHIAKI_NONE_DISABLED;
	info.auto_regist = false;
	info.packet_loss_max = 0.05; // tope de perdida reportada al servidor
	info.enable_idr_on_fec_failure = true;
	memcpy(info.psn_account_id, g_psn_account_id, sizeof(info.psn_account_id));

	snprintf(g_session_ip, sizeof(g_session_ip), "%s", host);
	logprintf("Sesion: conectando con %s (req_port %u informativo, chiaki usa 9295)",
		     host, (unsigned int)req_port);

	ChiakiErrorCode err = chiaki_session_init(&g_session, &info, &g_chiaki_log);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		logprintf("Sesion: init fallo: %s", chiaki_error_string(err));
		return false;
	}

	chiaki_session_set_event_cb(&g_session, session_event_cb, nullptr);
	chiaki_session_set_video_sample_cb(&g_session, video_sample_cb, nullptr);
	// Fase 3a: estado de video fresco para cada sesion (si la anterior
	// dejo el decoder de40 MB, aqui se libera antes del nuevo stream).
	wiiu_video_reset();
	// Mando en reposo al arrancar; el bucle principal vuelca VPAD a la
	// sesion cada frame (fase 3c) y el teclado del sistema lo devuelve
	// a reposo mientras esta abierto.
	ChiakiControllerState idle_state;
	chiaki_controller_state_set_idle(&idle_state);
	chiaki_session_set_controller_state(&g_session, &idle_state);

	// Fase 3b: sink de audio. El sink de chiaki trae opus crudo, así
	// que el decoder opus de la librería queda intercalado: chiaki →
	// opus decoder (PCM) → ring AX. Se fija antes del start.
	chiaki_opus_decoder_init(&g_opus_decoder, &g_chiaki_log);
	chiaki_opus_decoder_set_cb(&g_opus_decoder, session_opus_settings_cb,
				   session_opus_pcm_cb, nullptr);
	ChiakiAudioSink audio_sink = {};
	chiaki_opus_decoder_get_sink(&g_opus_decoder, &audio_sink);
	chiaki_session_set_audio_sink(&g_session, &audio_sink);
	audio_session_start();

	err = chiaki_session_start(&g_session);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		logprintf("Sesion: start fallo: %s", chiaki_error_string(err));
		chiaki_session_fini(&g_session);
		return false;
	}

	g_session_active = true;
	g_session_quit_received = false;
	g_session_login_pin_requested = false;
	g_video_frames = 0;
	g_tp_stream_seen = false; // el surface se mostrara con el1er frame
	g_tp_show_until = 0;      // oculto hasta entonces
	g_video_bytes = 0;
	g_video_frames_lost = 0;
	g_fec_failures = 0;
	g_video_log_ticks = OSGetTime();
	g_video_log_frames = 0;
	// Detector de estancamiento del decoder: ventana desde el arranque.
	{
		uint64_t now = OSGetTime();
		g_stall_fed = 0;
		g_stall_decoded = wiiu_video_stats().decoded;
		g_stall_last_fed_ticks = now;
		g_stall_last_dec_ticks = now;
		g_stall_last_idr_ticks = now;
		g_stall_idr_sent_ticks = 0;
	}
	// Fase 4a: al proximo arranque se preselecciona esta consola.
	chiaki_mutex_lock(&g_host_mutex);
	snprintf(g_config.last_host, sizeof(g_config.last_host), "%s", host);
	chiaki_mutex_unlock(&g_host_mutex);
	g_config_dirty = true;
	logprintf("Sesion: thread en marcha (↑ registra | A conecta | B cancela al conectar | icono abajo dcha = parar)");
	return true;
}

static void stop_session(bool shutdown = false)
{
	if(!g_session_active)
		return;
	logprintf("Sesion: parando...");
	chiaki_session_stop(&g_session);
	chiaki_session_join(&g_session);
	chiaki_session_fini(&g_session);
	// El hilo de sesión ya está unido: nadie entrega más opus. Libera
	// el decoder (pcm_buf + opus) y silencia la voz AX.
	chiaki_opus_decoder_fini(&g_opus_decoder);
	audio_session_stop();
	// Libera decoder h264 (~40 MB) y buffers; el hilo de video ya esta
	// unido, asi que nadie llama a wiiu_video_sample ahora. En shutdown
	// (HOME con stream) no se espera al GPU: ver wiiu_video_reset.
	wiiu_video_reset(!shutdown);
	g_session_active = false;
	g_session_login_pin_requested = false;
	logprintf("Sesion finalizada");
}

// ---------------------------------------------------------------------------
// Mando -> sesion (fase 3c). VPAD se vuelca a ChiakiControllerState cada
// frame; la cadencia de red la lleva el feedback sender de chiaki (solo
// nota cambios), asi que aqui no hay timers. Layout por posicion fisica
// del GamePad: sur B = cross, este A = circulo, oeste Y = cuadrado,
// norte X = triangulo (mismos sitios que en un DualShock). ZL/ZR son
// digitales en el GamePad -> L2/R2 a fondo. HOME lo consume WHBProc
// (salir de la app). MINUS (SELECT) lleva el boton PS de la PS4: en el
// menu sigue abriendo el editor de Account ID, con stream activo manda
// el PS. SHARE queda sin boton por ahora.
// ---------------------------------------------------------------------------
static int16_t stick_axis(float v)
{
	// Zona muerta del 8% (deriva del stick), reescalando el resto para
	// seguir llegando a tope.
	if(v > 1.0f)
		v = 1.0f;
	else if(v < -1.0f)
		v = -1.0f;
	const float dead = 0.08f;
	if(v > -dead && v < dead)
		return 0;
	v = (v > 0 ? v - dead : v + dead) / (1.0f - dead);
	return (int16_t)(v * 32767.0f);
}

static void session_send_input(const VPADStatus &status)
{
	ChiakiControllerState st;
	chiaki_controller_state_set_idle(&st);

	const uint32_t h = status.hold;
	if(h & VPAD_BUTTON_B)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_CROSS;
	if(h & VPAD_BUTTON_A)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_MOON;
	if(h & VPAD_BUTTON_Y)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_BOX;
	if(h & VPAD_BUTTON_X)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_PYRAMID;
	if(h & VPAD_BUTTON_LEFT)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT;
	if(h & VPAD_BUTTON_RIGHT)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT;
	if(h & VPAD_BUTTON_UP)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_UP;
	if(h & VPAD_BUTTON_DOWN)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN;
	if(h & VPAD_BUTTON_L)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_L1;
	if(h & VPAD_BUTTON_R)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_R1;
	if(h & VPAD_BUTTON_STICK_L)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_L3;
	if(h & VPAD_BUTTON_STICK_R)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_R3;
	if(h & VPAD_BUTTON_MINUS)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_PS;
	if(h & VPAD_BUTTON_PLUS)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_OPTIONS;
	st.l2_state = (h & VPAD_BUTTON_ZL) ? 0xff : 0;
	st.r2_state = (h & VPAD_BUTTON_ZR) ? 0xff : 0;

	// VPAD: +y = arriba; en chiaki (y SDL) arriba es negativo: el
	// escritorio pasa los ejes SDL tal cual a la PS4.
	st.left_x = stick_axis(status.leftStick.x);
	st.left_y = stick_axis(-status.leftStick.y);
	st.right_x = stick_axis(status.rightStick.x);
	st.right_y = stick_axis(-status.rightStick.y);

	// Zona touchpad superior derecha: el contacto se reinserta con su
	// id en el estado fresco (el state nuevo arranca sin toques); el
	// click es un pulso tras un toque corto y quieto (tap-to-click).
	if(g_tp_down)
	{
		st.touch_id_next = g_tp_id;
		chiaki_controller_state_start_touch(&st, g_tp_x, g_tp_y);
	}
	else if(OSGetTime() < g_tp_click_ticks)
		st.buttons |= CHIAKI_CONTROLLER_BUTTON_TOUCHPAD;

	chiaki_session_set_controller_state(&g_session, &st);
}

// Con el teclado del sistema abierto (PIN/ID): mando en reposo para que
// ningun boton quede pegado mientras se teclea.
static void session_send_idle(void)
{
	ChiakiControllerState st;
	chiaki_controller_state_set_idle(&st);
	chiaki_session_set_controller_state(&g_session, &st);
}

// Puerta de conexion: con la lista vacia A no conecta NUNCA aunque el
// discovery vea la consola por broadcast (evita cualquier estado
// heredado de una consola recien borrada). El motivo se ve en el pie.
static void refuse_a_empty_list(void)
{
	logprintf("A: lista de consolas vacia — Arriba para registrar");
	snprintf(g_last_session_msg, sizeof(g_last_session_msg),
		 "Sin consolas registradas - pulsa Arriba con el PIN");
}

static void handle_press_a()
{
	if(g_session_active)
	{
		logprintf("A: ya hay una sesion activa (B para parar)");
		return;
	}
	if(config_console_count(&g_config) <= 0)
	{
		refuse_a_empty_list();
		return;
	}
	if(!g_host_found && !g_host_ip[0])
	{
		logprintf("A: sin consola descubierta — X para fijar la IP");
		return;
	}

	chiaki_mutex_lock(&g_host_mutex);
	char host[48];
	snprintf(host, sizeof(host), "%s", g_host_addr);
	ChiakiDiscoveryHostState state = g_host_state;
	chiaki_mutex_unlock(&g_host_mutex);
	if(!g_host_found)
	{
		// Sin discovery no podemos ver standby: asumimos ready y
		// conectamos directo (el wakeup si requiere discovery).
		snprintf(host, sizeof(host), "%s", g_host_ip);
		state = CHIAKI_DISCOVERY_HOST_STATE_READY;
	}

	if(state == CHIAKI_DISCOVERY_HOST_STATE_STANDBY)
	{
		if(!g_registered)
		{
			logprintf("A: la PS4 esta en standby y no hay clave de registro");
			return;
		}
		char key_hex[CHIAKI_SESSION_AUTH_SIZE + 1];
		chiaki_mutex_lock(&g_host_mutex);
		memcpy(key_hex, g_regist_key, CHIAKI_SESSION_AUTH_SIZE);
		chiaki_mutex_unlock(&g_host_mutex);
		key_hex[CHIAKI_SESSION_AUTH_SIZE] = 0;
		uint64_t credential = strtoull(key_hex, NULL, 16);
		logprintf("A: despertando la PS4 %s ...", host);
		ChiakiErrorCode err = chiaki_discovery_wakeup(&g_chiaki_log, NULL, host,
							       credential, false);
		if(err != CHIAKI_ERR_SUCCESS)
			logprintf("A: wakeup fallo: %s", chiaki_error_string(err));
		return;
	}
	if(state != CHIAKI_DISCOVERY_HOST_STATE_READY)
	{
		logprintf("A: la PS4 aun no esta en estado ready (espera al discovery)");
		return;
	}
	start_session();
}

static void handle_press_up()
{
	if(g_session_active)
	{
		logprintf("↑: ya hay una sesion activa (B para parar antes)");
		return;
	}
	if(g_regist_state != RegistState::Idle)
	{
		logprintf("↑: ya hay un registro en curso");
		return;
	}

	chiaki_mutex_lock(&g_host_mutex);
	bool found = g_host_found;
	ChiakiTarget target = g_host_target;
	char addr[48];
	snprintf(addr, sizeof(addr), "%s", g_host_addr);
	chiaki_mutex_unlock(&g_host_mutex);

	logprintf("↑: discovery found=%d addr='%s' ip_manual='%s'", (int)found, addr,
		     g_host_ip);
	if(!found && !g_host_ip[0])
	{
		logprintf("↑: sin consola descubierta — X para fijar la IP de la PS4");
		return;
	}
	if(!found)
	{
		// Sin respuesta del discovery usamos la IP tecleada como destino
		// directo (regist y sesion aceptan cualquier host).
		target = CHIAKI_TARGET_PS4_10;
		logprintf("↑: discovery sin respuesta — destino directo %s (PS4 10.x)",
			     g_host_ip);
	}

	// PS4 antigua (< 7.0): regist con el ID online, sin account id.
	// Desconocida = por defecto moderna (start_regist la rechazara si falla
	// el parseo de la version).
	if(target != CHIAKI_TARGET_PS4_UNKNOWN && target < CHIAKI_TARGET_PS4_9)
	{
		if(!g_psn_online_id[0])
		{
			logprintf("↑: introduce tu ID publico de PSN");
			swkbd_show(Prompt::PsnId);
		}
		else
			swkbd_show(Prompt::Pin);
		return;
	}

	if(g_have_psn_account)
	{
		logprintf("↑: introduce el PIN de 8 digitos de la PS4");
		swkbd_show(Prompt::Pin);
	}
	else
	{
		logprintf("↑: introduce el Account ID de PSN en base64 (ej. wLEMeFoyimY=)");
		g_accountid_regist_flow = true;
		swkbd_show(Prompt::AccountId);
	}
}

static void swkbd_frame(VPADStatus &status)
{
	nn::swkbd::ControllerInfo controllerInfo;
	controllerInfo.vpad = &status;
	controllerInfo.kpad[0] = nullptr;
	controllerInfo.kpad[1] = nullptr;
	controllerInfo.kpad[2] = nullptr;
	controllerInfo.kpad[3] = nullptr;
	nn::swkbd::Calc(controllerInfo);

	if(nn::swkbd::IsNeedCalcSubThreadFont())
		nn::swkbd::CalcSubThreadFont();
	if(nn::swkbd::IsNeedCalcSubThreadPredict())
		nn::swkbd::CalcSubThreadPredict();

	// El swkbd retail es una maquina de estados impulsada por Calc: tras
	// DisappearInputForm hay que seguir bombeando Calc unos frames hasta
	// que la transicion de salida termine. Si paramos del tirón, el
	// proximo AppearInputForm no se llega a mostrar en hardware (en Cemu
	// el HLE no lo simula — por ahi encadenaba bien). visible sigue
	// true durante el bombeo para que Draw* siga pintando la salida.
	if(g_swkbd_hide_frames > 0)
	{
		if(--g_swkbd_hide_frames == 0)
		{
			g_swkbd_visible = false;
			g_prompt = Prompt::None;
		}
		return;
	}

	if(nn::swkbd::IsDecideOkButton(nullptr))
	{
		char input[64] = { 0 };
		const char16_t *str = nn::swkbd::GetInputFormString();
		if(str)
		{
			size_t i = 0;
			for(; str[i] && i < sizeof(input) - 1; i++)
				input[i] = str[i] < 0x80 ? (char)str[i] : '?';
		}

		Prompt p = g_prompt;
		nn::swkbd::DisappearInputForm();
		g_swkbd_hide_frames = SWKBD_HIDE_FRAMES;

		switch(p)
		{
		case Prompt::IP:
			if(valid_ipv4(input))
			{
				snprintf(g_host_ip, sizeof(g_host_ip), "%s", input);
				logprintf("IP de la consola fijada: %s", g_host_ip);
				restart_discovery();
			}
			else
			{
				logprintf("IP invalida: '%s' — reintenta", input);
				g_swkbd_reopen = Prompt::IP;
			}
			break;

		case Prompt::PsnId:
		{
			// Solo PS4 < 7.0: el ID online va directo al regist.
			if(!input[0])
			{
				logprintf("ID de PSN vacio — reintenta");
				g_swkbd_reopen = Prompt::PsnId;
				break;
			}
			snprintf(g_psn_online_id, sizeof(g_psn_online_id), "%s", input);
			logprintf("ID guardado — introduce el PIN de la PS4");
			g_swkbd_reopen = Prompt::Pin;
			break;
		}

		case Prompt::AccountId:
		{
			if(!parse_psn_account_id(input, g_psn_account_id))
			{
				logprintf("Account ID invalido: '%s' — formato base64 (ej. wLEMeFoyimY=)", input);
				g_swkbd_reopen = Prompt::AccountId;
				break;
			}
			g_have_psn_account = true;
			char hex[17] = { 0 };
			for(size_t i = 0; i < sizeof(g_psn_account_id); i++)
				snprintf(hex + i * 2, 3, "%02x", g_psn_account_id[i]);
			// El config se queda en hex (formato del archivo); el
			// log muestra tambien el base64 (wLEMeFoyimY=), que es
			// el formato que sirve la API de PSN.
			char b64[13] = { 0 };
			if(chiaki_base64_encode(g_psn_account_id,
					      sizeof(g_psn_account_id), b64,
					      sizeof(b64)) != CHIAKI_ERR_SUCCESS)
				b64[0] = 0;
			logprintf("Account ID guardado: %s (base64: %s)", hex, b64);
			// Fase 4a: persistir la cuenta en config.ini.
			snprintf(g_config.account_id_hex,
				 sizeof(g_config.account_id_hex), "%s", hex);
			g_config_dirty = true;
			if(g_accountid_regist_flow)
			{
				logprintf("Introduce el PIN de 8 digitos de la PS4");
				g_swkbd_reopen = Prompt::Pin;
			}
			else
				logprintf("Cuenta actualizada — A conecta con el nuevo ID");
			g_accountid_regist_flow = false;
			break;
		}

		case Prompt::Pin:
		{
			size_t len = strlen(input);
			bool digits = len == 8;
			for(size_t i = 0; digits && i < len; i++)
				digits = input[i] >= '0' && input[i] <= '9';
			if(!digits)
			{
				logprintf("PIN invalido: '%s' — requiere 8 digitos", input);
				g_swkbd_reopen = Prompt::Pin;
				break;
			}
			start_regist((uint32_t)strtoul(input, NULL, 10));
			break;
		}

		case Prompt::LoginPin:
			if(!input[0])
			{
				logprintf("PIN de acceso vacio — reintenta");
				g_swkbd_reopen = Prompt::LoginPin;
				break;
			}
			if(g_session_active)
			{
				ChiakiErrorCode err = chiaki_session_set_login_pin(
					&g_session, (const uint8_t *)input, strlen(input));
				if(err != CHIAKI_ERR_SUCCESS)
					logprintf("PIN de acceso: fallo %s",
						     chiaki_error_string(err));
				else
					logprintf("PIN de acceso enviado a la PS4");
			}
			g_session_login_pin_requested = false;
			break;

		default:
			break;
		}
	}
	else if(nn::swkbd::IsDecideCancelButton(nullptr))
	{
		Prompt p = g_prompt;
		nn::swkbd::DisappearInputForm();
		g_swkbd_hide_frames = SWKBD_HIDE_FRAMES;

		switch(p)
		{
		case Prompt::IP:
			logprintf("IP no cambiada");
			break;
		case Prompt::PsnId:
			logprintf("Registro cancelado (sin ID PSN)");
			break;
		case Prompt::AccountId:
			if(g_accountid_regist_flow)
				logprintf("Registro cancelado (sin Account ID)");
			else
				logprintf("Account ID no cambiado");
			g_accountid_regist_flow = false;
			break;
		case Prompt::Pin:
			logprintf("Registro cancelado (sin PIN)");
			break;
		case Prompt::LoginPin:
			g_session_login_pin_requested = false;
			logprintf("PIN de acceso no introducido (la PS4 puede rechazar)");
			break;
		default:
			break;
		}
	}
}

static void swkbd_shutdown()
{
	if(g_swkbd_created)
	{
		nn::swkbd::Destroy();
		g_swkbd_created = false;
	}
	if(g_swkbd_work_mem)
	{
		MEMFreeToDefaultHeap(g_swkbd_work_mem);
		g_swkbd_work_mem = nullptr;
	}
	if(g_fs_client)
	{
		FSDelClient(g_fs_client, FS_ERROR_FLAG_NONE);
		MEMFreeToDefaultHeap(g_fs_client);
		g_fs_client = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Menu principal: se dibuja en TV y DRC cuando no hay sesion activa.
// Replica el look de chiaki-ng (gui/src/qml/Main.qml): tema Qt Material
// Dark (fondo #121212, ToolBar #1e1e1e, acento #00a7ff) con su ToolBar de
// botones planos, filas tipo ItemDelegate de180px con el icono PS4 real
// (console-ps4.svg: cuerpo + tira de luz #light_on/#light_standby) y la
// columna de acciones de la fila destacada. Izq/Dcha cambia la seleccion
// (apply_console vuelca IP+claves a los globals con los que conecta A).
// ---------------------------------------------------------------------------
#define APP_BUILD_TAG "gui-49"

// Chip de boton del gamepad: disco (mask en la mitad derecha del atlas)
// + letra centrada. Colores robustos (gris medio + letra blanca): si en
// consola la mask sale completa o solo el aro (gui-10), la blanca se lee
// en ambos casos. Devuelve el ancho dibujado; el caller gestiona el
// hueco posterior.
static float draw_button_chip(float x, float y, int scale, char letter,
			      uint32_t disc, uint32_t letter_color)
{
	float du, dv, dw, dh;
	gui_asset_uvs(GUI_ASSET_DISC, &du, &dv, &dw, &dh);
	float d = (float)(scale * 12); // disco: 24 px TV / 12 px DRC
	gui_image(x, y, d, d, du, dv, dw, dh, disc);
	char s[2] = { letter, 0 };
	float tw = gui_text_width(s, scale);
	gui_text(x + (d - tw) / 2.f, y + (d - (float)(scale * 8)) / 2.f,
		 s, letter_color, scale);
	return d;
}

// Overlay "Conectando..." mientras la sesion arranca: desde A hasta el
// primer frame decodificado quedaba ~1 s de clear verde sin nada encima.
// Tarjeta Material (#1e1e1e) centrada, como los dialogs de chiaki-ng.
static void draw_connecting(int w, int h)
{
	int scale = (w >= 1280) ? 2 : 1;
	int u = 8 * scale;

	// Puntos animados (~270 ms por fase) como feedback de vida.
	static const char *dots[] = { "", ".", "..", "..." };
	int phase = (int)((OSGetTime() >> 24) & 3);
	char line[80];
	snprintf(line, sizeof(line), "Conectando con %s%s",
		 g_session_ip[0] ? g_session_ip : "la consola", dots[phase]);
	const char *hint = "para cancelar";

	gui_begin(w, h);

	float tw = gui_text_width(line, scale);
	float chip_d = (float)(scale * 12), chip_gap = (float)(scale * 4);
	float hw = chip_d + chip_gap + gui_text_width(hint, scale);
	float cw = (tw > hw ? tw : hw) + (float)(u * 3);
	float ch = (float)(u * 5); // dos lineas + padding
	float cx = ((float)w - cw) / 2.f;
	float cy = ((float)h - ch) / 2.f;

	gui_rect(cx, cy, cw, ch, GUI_RGBA(0x1e, 0x1e, 0x1e, 0xff));
	gui_text(cx + (cw - tw) / 2.f, cy + (float)u, line,
		 GUI_RGBA(0xff, 0xff, 0xff, 0xde), scale);
	float bx = cx + (cw - hw) / 2.f;
	draw_button_chip(bx, cy + (float)(u * 3) - (chip_d - (float)u) / 2.f,
			 scale, 'B', GUI_RGBA(0x54, 0x54, 0x54, 0xff),
			 GUI_RGBA(0xff, 0xff, 0xff, 0xde));
	gui_text(bx + chip_d + chip_gap, cy + (float)(u * 3), hint,
		 GUI_RGBA(0xff, 0xff, 0xff, 0x8a), scale);

	gui_flush();
}

// Rect del icono de parada en la resolucion dada: tamano u*4 y margen
// u*2 (TV: 64 px en la esquina; DRC: 32 px). El toque se evalua contra
// la caja DRC (854x480), la unica pantalla con tactil.
static void stop_button_rect(int w, int h, int *x, int *y, int *d)
{
	int scale = (w >= 1280) ? 2 : 1;
	int u = 8 * scale;
	*d = u * 4;
	*x = w - u * 2 - *d;
	*y = h - u * 2 - *d;
}

// Rect de la zona táctil del touchpad (esquina superior derecha):
// u*30 x u*15 (aspecto ~2:1, como la superficie del SVG). Con el
// dedo se maneja bien: 240x120 px en la pantalla del DRC.
static void touchpad_rect(int w, int h, int *x, int *y, int *rw, int *rh)
{
	int scale = (w >= 1280) ? 2 : 1;
	int u = 8 * scale;
	*rw = u * 30;
	*rh = u * 15;
	*x = w - u * 2 - *rw;
	*y = u * 2;
}

// Icono de parada (solo con sesion activa): disco translucido con el
// glifo de apagado centrado, legible sobre cualquier frame de video.
// Gui_begin/flush propio como draw_connecting, para poder pintarlo
// despues del frame de video.
static void draw_stop_button(int w, int h)
{
	int x, y, d;
	stop_button_rect(w, h, &x, &y, &d);

	float du, dv, dw, dh;
	gui_asset_uvs(GUI_ASSET_DISC, &du, &dv, &dw, &dh);
	float ou, ov, ow, oh;
	gui_asset_uvs(GUI_ASSET_OFF, &ou, &ov, &ow, &oh);

	gui_begin(w, h);
	gui_image((float)x, (float)y, (float)d, (float)d, du, dv, dw, dh,
		  GUI_RGBA(0x00, 0x00, 0x00, 0xb3));
	int g = d - d / 3; // glifo a dos tercios del disco
	gui_image((float)(x + (d - g) / 2), (float)(y + (d - g) / 2),
		  (float)g, (float)g, ou, ov, ow, oh,
		  GUI_RGBA(0xff, 0xff, 0xff, 0xe6));
	gui_flush();
}

// Superficie del touchpad (solo en sesion): redondeada (mascara del
// SVG) y se aclara mientras hay un contacto encima.
static void draw_touchpad(int w, int h)
{
	// Transitorio: visible durante la ventana de 2 s (inicio del
	// stream o contacto en la zona); al vencer hace un fundido de
	// salida rapido hasta transparente y ya no se pinta.
	uint64_t now = OSGetTime();
	uint64_t fade_ticks = OSMillisecondsToTicks(250);
	if(now >= g_tp_show_until + fade_ticks)
		return;
	float fade = 1.0f;
	if(now >= g_tp_show_until)
		fade = 1.0f - (float)(now - g_tp_show_until) /
				     (float)fade_ticks;
	int x, y, rw, rh;
	touchpad_rect(w, h, &x, &y, &rw, &rh);
	float tu, tv, tw, th;
	gui_asset_uvs(GUI_ASSET_TOUCHPAD, &tu, &tv, &tw, &th);
	gui_begin(w, h);
	// Gris oscuro tipo tarjeta Material: un gris claro translucido
	// heredaba el celeste del juego por debajo (al 30% de alpha
	// se veia 70% de juego); con mas cuerpo el celeste pasa a gris
	// azulado oscuro. Con dedo encima se aclara a gris medio para
	// localizar el toque. El alpha se multiplica por el fundido de
	// salida.
	uint32_t col = g_tp_down ? GUI_RGBA(0x94, 0x94, 0x94, 0)
				 : GUI_RGBA(0x3d, 0x3d, 0x3d, 0);
	uint32_t a = (uint32_t)((g_tp_down ? 0xbf : 0x99) * fade + 0.5f);
	gui_image((float)x, (float)y, (float)rw, (float)rh, tu, tv, tw, th,
		  col | (a << 24));
	gui_flush();
}

static void draw_menu(int w, int h)
{
	// Escala2x en TV (>=1280) y1x en DRC: la fuente8x8 queda legible
	// en ambas salidas.
	int scale = (w >= 1280) ? 2 : 1;
	int u = 8 * scale; // celda de fuente
	int m = w / 40;    // margen lateral (TV32, DRC21)

	gui_begin(w, h);

	// Paleta Qt Material Dark de chiaki-ng (Main.qml: Material.theme
	// Dark + Material.accent "#00a7ff"); opacidades tipicas del tema
	// (texto87/54/38%) y luz de consola del SVG console-ps4.
	const uint32_t c_bg = GUI_RGBA(0x12, 0x12, 0x12, 0xff);     // fondo
	const uint32_t c_bar = GUI_RGBA(0x1e, 0x1e, 0x1e, 0xff);    // ToolBar
	const uint32_t c_text = GUI_RGBA(0xff, 0xff, 0xff, 0xde);   //87%
	const uint32_t c_text2 = GUI_RGBA(0xff, 0xff, 0xff, 0x8a);  //54%
	const uint32_t c_text3 = GUI_RGBA(0xff, 0xff, 0xff, 0x61);  //38%
	const uint32_t c_white = GUI_RGBA(0xff, 0xff, 0xff, 0xff);
	const uint32_t c_accent = GUI_RGBA(0x00, 0xa7, 0xff, 0xff);
	const uint32_t c_standby = GUI_RGBA(0xff, 0xae, 0x2f, 0xff);
	const uint32_t c_sel = GUI_RGBA(0x00, 0xa7, 0xff, 0x33); // highlight
	const uint32_t c_icon = GUI_RGBA(0x30, 0x30, 0x30, 0xff); // cuerpo
	const uint32_t c_off = GUI_RGBA(0x40, 0x40, 0x40, 0xff);  // luz off
	const uint32_t c_chip = GUI_RGBA(0x54, 0x54, 0x54, 0xff); // pastilla
	const uint32_t c_chip_fg = GUI_RGBA(0xff, 0xff, 0xff, 0xde);

	// UVs de los assets del atlas (wordmark + PS4).
	float lu, lv, lw, lh, bu, bv, bw, bh, su, sv, sw, sh;
	gui_asset_uvs(GUI_ASSET_LOGO, &lu, &lv, &lw, &lh);
	gui_asset_uvs(GUI_ASSET_PS4_BODY, &bu, &bv, &bw, &bh);
	gui_asset_uvs(GUI_ASSET_PS4_LIGHT, &su, &sv, &sw, &sh);

	// Fondo + ToolBar (chiaki-ng: barra de80/1080 con botones planos).
	gui_rect(0.f, 0.f, (float)w, (float)h, c_bg);
	int bar_h = u * 3 + u / 2;
	gui_rect(0.f, 0.f, (float)w, (float)bar_h, c_bar);
	// Divisor sutil bajo la barra (elevation de Material: 1dp).
	gui_rect(0.f, (float)bar_h, (float)w, (float)scale,
		 GUI_RGBA(0x2a, 0x2a, 0x2a, 0xff));

	// Wordmark de chiaki-ng (asset real) a la izquierda de la barra.
	float logo_h = (float)(u * 5 / 2);
	float logo_w = logo_h * (128.f / 76.f); // aspecto del asset
	gui_image((float)m, ((float)bar_h - logo_h) / 2.f, logo_w, logo_h,
		  lu, lv, lw, lh, c_white);

	// Botones planos del ToolBar (chiaki-ng) con chips de boton del
	// gamepad para los que tienen tecla propia (SELECT, X, Y); HOME
	// y las flechas del D-pad van en texto.
	struct TbSeg
	{
		char chip;
		const char *label;
	};
	// Segmento "Calidad" con el preset vigente a la vista (ZL/ZR lo
	// cicla; label dinámico, por eso el array no es const).
	char quality_lbl[40];
	snprintf(quality_lbl, sizeof(quality_lbl), "Calidad %s",
		 video_preset_label(video_preset_cur()));
	const TbSeg tb_segs[] = {
		{ 0, "Registrar (Arriba)" },
		{ '-', "ID" },
		{ 'X', "IP" },
		{ 'Y', "Buscar" },
		{ 'Z', quality_lbl },
		{ '+', "Eliminar" },
		{ 0, "Salir (HOME)" },
	};
	int nseg = (int)(sizeof(tb_segs) / sizeof(tb_segs[0]));
	float chip_d = (float)(scale * 12);
	// Hueco chip->etiqueta y entre segmentos ampliado: con el hueco
	// previo no quedaba claro a que opcion apuntaba cada disco.
	float chip_gap = (float)(scale * 6);
	float seg_gap = (float)u * 2.f;
	float total = 0;
	for(int i = 0; i < nseg; i++)
	{
		if(i)
			total += seg_gap;
		if(tb_segs[i].chip)
			total += chip_d + chip_gap;
		total += gui_text_width(tb_segs[i].label, scale);
	}
	float ty = ((float)bar_h - (float)u) / 2.f;
	float tx = (float)(w - m) - total;
	// Con el dialogo de borrado abierto la toolbar se oculta: su
	// etiqueta "Eliminar" (chip +) quedaba fuera del cuadro de
	// confirmacion y se confundia con la accion del dialogo.
	if(!g_confirm_del)
	{
		for(int i = 0; i < nseg; i++)
		{
			if(tb_segs[i].chip)
			{
				draw_button_chip(tx,
						 ty - (chip_d - (float)u) /
							 2.f,
						 scale, tb_segs[i].chip,
						 c_chip, c_chip_fg);
				tx += chip_d + chip_gap;
			}
			gui_text(tx, ty, tb_segs[i].label, c_text, scale);
			tx += gui_text_width(tb_segs[i].label, scale);
			if(i + 1 < nseg)
				tx += seg_gap;
		}
	}

	int count = config_console_count(&g_config);
	int sel = (g_menu_sel >= 0 && g_menu_sel < count) ? g_menu_sel : 0;
	int list_y = bar_h + u * 2;
	int row_h = u * 7; // filas tipo ItemDelegate (180/1080 de chiaki)
	int max_rows = (h - u * 4 - list_y) / row_h;
	if(max_rows < 2)
		max_rows = 2;
	int rows = count == 0 ? 2 : (count < max_rows ? count : max_rows);

	// Snapshot del discovery de la consola SELECCIONADA (cb desde su
	// thread; el filtro unicast la dirige a su IP).
	chiaki_mutex_lock(&g_host_mutex);
	bool found = g_host_found;
	ChiakiDiscoveryHostState st = g_host_state;
	chiaki_mutex_unlock(&g_host_mutex);

	if(count == 0)
	{
		// Icono de discovery de chiaki-ng (discover-24px) centrado
		// sobre el aviso: encendido (acento) si hay consola
		// detectada, apagado si aun no se ve ninguna.
		float ru, rv, rw, rh;
		gui_asset_uvs(found ? GUI_ASSET_RADAR_ON : GUI_ASSET_RADAR_OFF,
			       &ru, &rv, &rw, &rh);
		float isz = (float)(u * 5);
		gui_image(((float)w - isz) / 2.f, (float)(list_y + u * 2),
			  isz, isz, ru, rv, rw, rh,
			  found ? c_accent : c_text3);
		if(found)
		{
			// Aviso unico y directo: detectada -> ↑ registra.
			const char *msg =
				"Consola detectada - pulsa Arriba para registrar";
			gui_text(((float)w - gui_text_width(msg, scale)) / 2.f,
				 (float)(list_y + u * 8), msg, c_text2,
				 scale);
		}
		else
		{
			const char *empty =
				"Sin consolas registradas - pulsa Arriba con el PIN de la PS4";
			gui_text(((float)w - gui_text_width(empty, scale)) / 2.f,
				 (float)(list_y + u * 8), empty, c_text3,
				 scale);
			const char *seek = "Buscando consolas en la red...";
			gui_text(((float)w - gui_text_width(seek, scale)) / 2.f,
				 (float)(list_y + u * 10), seek, c_text3,
				 scale);
		}
	}
	else
	{
		// Ventana de visibilidad si hay mas consolas que filas.
		int start = sel - rows + 1;
		if(start < 0)
			start = 0;
		for(int i = 0; i < rows; i++)
		{
			int idx = start + i;
			const ConfigConsole *con = &g_config.consoles[idx];
			int y = list_y + i * row_h;
			bool is_sel = (idx == sel);
			bool live = is_sel && found;

			// ItemDelegate destacado de Material: acento suave
			// + barra vertical como en el ListView de chiaki-ng.
			if(is_sel)
			{
				gui_rect(0.f, (float)y, (float)w,
					 (float)row_h, c_sel);
				gui_rect(0.f, (float)y, (float)(u / 4),
					 (float)row_h, c_accent);
			}

			// Icono PS4 real: cuerpo + tira de luz teñida por
			// estado (azul encendida, ámbar standby, apagada si
			// no hay discovery), igual que #light_on/#light_standby.
			uint32_t light = c_off;
			if(live)
			{
				if(st == CHIAKI_DISCOVERY_HOST_STATE_READY)
					light = c_accent;
				else if(st ==
					CHIAKI_DISCOVERY_HOST_STATE_STANDBY)
					light = c_standby;
			}
			float icon_w = (float)(u * 8);
			float icon_h = icon_w * (22.f / 128.f);
			float icon_x = (float)(m + u);
			float icon_y = (float)y +
				      ((float)row_h - icon_h) / 2.f;
			gui_image(icon_x, icon_y, icon_w, icon_h, bu, bv, bw,
				  bh, c_icon);
			gui_image(icon_x, icon_y, icon_w, icon_h, su, sv, sw,
				  sh, light);

			// Labels del delegate: nombre / dirección / estado.
			int tx = m + u * 10;
			int lh2 = u * 2;
			int ty = y + (row_h - lh2 * 3) / 2;
			gui_text((float)tx, (float)ty,
				 con->name[0] ? con->name : "?", c_text,
				 scale);
			char line[72];
			snprintf(line, sizeof(line), "Dirección: %s",
				 con->host);
			gui_text((float)tx, (float)(ty + lh2), line, c_text2,
				 scale);
			const char *state;
			if(!is_sel)
				state = "Estado: -";
			else if(g_regist_state != RegistState::Idle)
				state = "Estado: registrando...";
			else if(!found)
				state = "Estado: sin discovery (Y=buscar)";
			else if(st == CHIAKI_DISCOVERY_HOST_STATE_READY)
				state = "Estado: lista";
			else if(st == CHIAKI_DISCOVERY_HOST_STATE_STANDBY)
				state = "Estado: en espera";
			else
				state = "Estado: discovery";
			gui_text((float)tx, (float)(ty + lh2 * 2), state,
				 c_text2, scale);

			// Acciones de la fila destacada (chiaki-ng: columna
			// de botones planos a la derecha: Conectar/Wake Up),
			// con chip A en color acento.
			if(is_sel)
			{
				const char *act =
					(found && st ==
						 CHIAKI_DISCOVERY_HOST_STATE_STANDBY)
						? "Despertar"
						: "Conectar";
				float ax = (float)(w - m) - chip_d -
					   chip_gap - gui_text_width(act, scale);
				float ay = (float)(y + (row_h - u) / 2);
				draw_button_chip(ax, ay - (chip_d - (float)u) / 2.f,
						 scale, 'A', c_accent, c_white);
				gui_text(ax + chip_d + chip_gap, ay, act,
					 c_accent, scale);
			}
		}
	}

	// Indicador de posicion "n/m" cuando la ventana no muestra toda
	// la lista (arriba a la derecha, sobre la primera fila).
	if(count > rows)
	{
		char pos[16];
		snprintf(pos, sizeof(pos), "%d/%d", sel + 1, count);
		gui_text((float)(w - m) - gui_text_width(pos, scale),
			 (float)(list_y - u), pos, c_text3, scale);
	}

	// Pie (chiaki-ng: pista abajo-izquierda + etiqueta de version
	// abajo-derecha). Con la lista vacia la pista de navegacion no
	// aplica: se indica el flujo de registro.
	const char *foot = g_regist_state != RegistState::Idle
				   ? "Registrando... introduce el PIN que muestra la PS4"
				   : (count <= 0
					      ? "Arriba: registrar con el PIN de la PS4"
					      : "Izq/Dcha: elegir consola");
	gui_text((float)m, (float)(h - m - u), foot, c_text2, scale);
	if(g_last_session_msg[0])
		gui_text((float)m, (float)(h - m - u * 2), g_last_session_msg,
			 c_text3, scale);
	gui_text((float)(w - m) - gui_text_width(APP_BUILD_TAG, scale),
		 (float)(h - m - u), APP_BUILD_TAG, c_text3, scale);

	// Dialogo modal de confirmacion de borrado (PLUS en la toolbar):
	// scrim + carta Material centrada; fila inferior con [B] volver y
	// [A] Eliminar lado a lado (sin navegacion ni Cancelar: eso es B).
	// La toolbar se oculta mientras este abierto (su "Eliminar" del
	// chip + quedaba fuera del cuadro).
	if(g_confirm_del && count > 0)
	{
		gui_rect(0.f, 0.f, (float)w, (float)h,
			 GUI_RGBA(0, 0, 0, 0x99));

		float cw = (float)u * 56, ch = (float)u * 16;
		float cx = ((float)w - cw) / 2.f;
		float cy = ((float)h - ch) / 2.f;
		gui_rect(cx, cy, cw, ch, GUI_RGBA(0x1e, 0x1e, 0x1e, 0xff));

		const ConfigConsole *dc = &g_config.consoles[sel];
		gui_text(cx + (float)u * 2, cy + (float)u * 2,
			 "¿Eliminar consola?", c_text, scale);
		char msg[112];
		snprintf(msg, sizeof(msg), "Se eliminará '%.24s' de la lista.",
			 dc->name[0] ? dc->name : "?");
		gui_text(cx + (float)u * 2, cy + (float)u * 5, msg, c_text2,
			 scale);
		snprintf(msg, sizeof(msg), "Dirección: %.32s", dc->host);
		gui_text(cx + (float)u * 2, cy + (float)u * 7, msg, c_text3,
			 scale);

		// Fila inferior de acciones, juntas a la izquierda:
		// [B] volver ... [A] Eliminar (A confirma directamente).
		// Etiqueta en gris como "volver" (con c_accent no salia en
		// consola; el disco A ya lleva su acento).
		float hy = cy + (float)u * 10;
		float hx = cx + (float)u * 2;
		draw_button_chip(hx, hy - (chip_d - (float)u) / 2.f, scale,
				 'B', GUI_RGBA(0x54, 0x54, 0x54, 0xff),
				 GUI_RGBA(0xff, 0xff, 0xff, 0xde));
		hx += chip_d + chip_gap;
		gui_text(hx, hy, "volver", c_text2, scale);
		hx += gui_text_width("volver", scale) + (float)u * 2;
		draw_button_chip(hx, hy - (chip_d - (float)u) / 2.f, scale,
				 'A', c_accent, c_white);
		hx += chip_d + chip_gap;
		gui_text(hx, hy, "Eliminar", c_text2, scale);
	}

	gui_flush();
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	WHBProcInit();
	WHBLogCafeInit();

	// socket_lib_init ANTES de WHBLogUdpInit: este abre un socket UDP y
	// fallaria silenciosamente sin la red de Cafe OS inicializada.
	// Es void; no devuelve error (se valida implicitamente al usar sockets).
	socket_lib_init();

	WHBLogUdpInit();

	// Vuelca motivo, registros y backtrace del crash por WHBLog
	// (=> log.txt de la SD + UDP 4405). Tras los inits de log para que el
	// vuelco tenga a donde ir; sin esto un crash en consola real solo se
	// ve como "la app cerro sola".
	WHBInitCrashHandler();

	// Log de chiaki-lib → consola cafe + SD. INFO+WARNING+ERROR: los pings del
	// servicio no generan ruido (sus logs VERBOSE/DEBUG están comentados).
	chiaki_log_init(&g_chiaki_log,
		       CHIAKI_LOG_INFO | CHIAKI_LOG_WARNING | CHIAKI_LOG_ERROR,
		       wiiu_chiaki_log_cb, nullptr);

	chiaki_mutex_init(&g_host_mutex, false);

	logprintf("chiaki-ng Wii U: frontend starting");

	// Orden de init identico al build validado en consola (fase 2); la
	// consola en pantalla y el log SD se insertan sin mover nada mas.
	logprintf("init: WHBGfxInit");
	WHBGfxInit();
	VPADInit();
	logprintf("init: FSInit");
	FSInit();

	// Log a la SD: fs:/vol/external01/wiiu/apps/chiaki/log.txt. Antes
	// del bloque AX/audio: el diagnostico de arranque de la voz (offsets
	// leidos del DSP, AXCheckVoiceOffsets) debe llegar al archivo.
	logprintf("init: log SD");
	log_file_init();

	// Mixer AX a 48 kHz: la PS4 manda PCM a 48k y la voz AX no tiene
	// SRC propia (la frecuencia es la del mixer).
	logprintf("init: AXInit48k");
	AXInitParams ax_params = {};
	ax_params.renderer = AX_INIT_RENDERER_48KHZ;
	ax_params.pipeline = AX_INIT_PIPELINE_SINGLE;
	AXInitWithParams(&ax_params);
	audio_set_log(audio_log_shim);
	logprintf("init: audio %s", audio_init() ? "ok" : "FALLO");

	// Teclado del sistema (IP, PSN, PINs).
	logprintf("init: swkbd");
	swkbd_init();

	// Tras log_file_init para que el marker llegue a la SD (antes solo
	// iba a OSReport/UDP y no servia para confirmar el build desplegado).
	logprintf("BUILD wiiu-2026-10-16 " APP_BUILD_TAG);
	wiiu_video_set_log(video_log_sink);

	// Fase 4a: consolas registradas y cuenta guardadas en la SD. Se carga
	// antes del discovery para que la IP preseleccionada filtre el ping.
	logprintf("init: config");
	config_init(&g_config);
	config_load_sd();

	// Requiere socket_lib_init ya hecho (crea sockets y threads).
	logprintf("init: discovery");
	g_discovery_running = start_discovery(nullptr);

	// Menu GX2 (fase 4b): shaders + fuente embebida. Tras el log para
	// que un fallo quede registrado; sin GUI el menu no se dibuja y
	// el resto de la app sigue funcionando (las primitivas son no-op).
	logprintf("init: gui %s", gui_init() ? "ok" : "FALLO");

	logprintf("Controles: A=conectar  Izq/Dcha=elegir consola  Arriba=registrar  SELECT=ID  X=IP  Y=broadcast  ZL/ZR=calidad  icono abajo dcha (tocar)=parar sesion");

	uint32_t last_hold = 0;
	VPADTouchData last_touch = {};
	// Icono de parada táctil: transicion del ultimo toque en la caja.
	bool stop_touch_prev = false;

	while(WHBProcIsRunning())
	{
		g_main_iters++;
		// Vuelco del log a la SD en lotes (el handler solo encola):
		// corriendo aqui tambien llegan los logs de arranque y de
		// menu, y el hilo de red jamas toca FS.
		logq_drain_batch();
		// Reabrir el teclado tras una entrada invalida o para encadenar
		// formularios (ID PSN -> PIN).
		if(g_swkbd_reopen != Prompt::None && !g_swkbd_visible)
		{
			Prompt next = g_swkbd_reopen;
			g_swkbd_reopen = Prompt::None;
			swkbd_show(next);
		}

		// El registro termino (su thread mando el evento): join.
		if(g_regist_state == RegistState::Finished)
		{
			chiaki_regist_fini(&g_regist);
			g_regist_state = RegistState::Idle;
			// Resaltar en el menu la consola recien registrada
			// (el cb corrio en su thread; aqui ya esta todo quieto).
			int idx = config_find(&g_config, g_regist_target_host);
			if(idx >= 0)
				g_menu_sel = idx;
		}

		// Fase 4a: guardar cambios pendientes (registro nuevo, cuenta
		// PSN o ultima consola conectada).
		if(g_config_dirty)
		{
			g_config_dirty = false;
			config_save_sd();
		}

		// La PS4 pide PIN de acceso: teclado (si no hay otro form abierto).
		if(g_session_login_pin_requested && !g_swkbd_visible &&
		   g_swkbd_reopen == Prompt::None)
			swkbd_show(Prompt::LoginPin);

		// La sesion acabo sola (evento QUIT desde su thread): teardown.
		if(g_session_quit_received)
		{
			g_session_quit_received = false;
			if(g_session_active)
				stop_session();
		}

		VPADStatus status;
		VPADReadError error;
		// VPADRead es void; el resultado va en outError.
		VPADRead(VPAD_CHAN_0, &status, 1, &error);
		if(error == VPAD_READ_SUCCESS)
		{
			if(g_swkbd_visible)
			{
				// swkbd necesita el punto de táctil calibrado.
				VPADGetTPCalibratedPoint(VPAD_CHAN_0, &status.tpNormal,
							 &status.tpNormal);
				swkbd_frame(status);
			}
			else
			{
				// Rising edges antes de que log_vpad_changes actualice last_hold.
				uint32_t rising = status.hold & ~last_hold;

				// Con stream en marcha el mando es SOLO del juego: ni
				// acciones del menu ni logs por pulso (cada boton de
				// juego llenaria log.txt). last_hold sigue al dia para
				// no desencadenar acciones fantasma al volver. Parar es
				// el icono táctil; mientras dure la conexion (aun sin
				// frames) manda la tarjeta: B cancela.
				bool streaming = g_session_active && g_video_frames > 0;

				if(streaming)
					last_hold = status.hold;
				else
					log_vpad_changes(status, last_hold, last_touch);

				// Modal de borrado sin navegacion: A
				// confirma, B vuelve; el resto se ignora.
				if(streaming)
				{
					// El menu no consume nada con stream activo.
				}
				else if(g_confirm_del)
				{
					if(rising & VPAD_BUTTON_B)
						g_confirm_del = false;
					else if(rising & VPAD_BUTTON_A)
					{
						delete_selected_console();
						g_confirm_del = false;
					}
				}
				else if(rising & VPAD_BUTTON_PLUS)
				{
					// PLUS: borrado de la consola
					// seleccionada, con confirmacion.
					int count = config_console_count(
						&g_config);
					if(g_session_active)
						logprintf("PLUS: en sesion (B para parar)");
					else if(g_regist_state !=
						RegistState::Idle)
						logprintf("PLUS: regist en curso — espera a que termine");
					else if(count <= 0)
						logprintf("PLUS: no hay consolas registradas");
					else
					{
						if(g_menu_sel < 0 ||
						   g_menu_sel >= count)
							g_menu_sel = 0;
						g_confirm_del = true;
						logprintf(
							"PLUS: eliminar consola %d/%d — dialogo abierto (A/B)",
							g_menu_sel + 1, count);
					}
				}
				else if(rising & VPAD_BUTTON_X)
				{
					logprintf("X: abrir teclado para IP de la consola");
					swkbd_show(Prompt::IP);
				}
				else if(rising & VPAD_BUTTON_Y)
				{
					if(g_host_ip[0])
					{
						g_host_ip[0] = 0;
						logprintf("Y: IP borrada — modo broadcast");
						restart_discovery();
					}
					else
						logprintf("Y: ya estás en modo broadcast");
				}
				else if(rising & VPAD_BUTTON_UP)
					handle_press_up();
				else if(rising & VPAD_BUTTON_A)
					handle_press_a();
				else if(rising & VPAD_BUTTON_B)
				{
					if(g_session_active)
						stop_session();
					else
						logprintf("B: no hay sesion activa");
				}
				else if(rising & VPAD_BUTTON_MINUS)
				{
					// SELECT: editar el Account ID en
					// cualquier momento (requisito del
					// puerto: editable in-app); el flujo
					// de regist solo lo pedia si faltaba.
					if(g_session_active)
						logprintf("SELECT: en sesion (B para parar)");
					else if(g_regist_state !=
							RegistState::Idle)
						logprintf("SELECT: regist en curso — espera a que termine");
					else
					{
						logprintf("SELECT: editar Account ID de PSN");
						g_accountid_regist_flow = false;
						swkbd_show(Prompt::AccountId);
					}
				}
				else if(rising & (VPAD_BUTTON_LEFT |
						  VPAD_BUTTON_RIGHT))
				{
					if(g_session_active)
						logprintf("Izq/Dcha: en sesion (B para parar)");
					else
					{
						int count =
							config_console_count(&g_config);
						if(count <= 0)
							logprintf(
								"Sin consolas: Arriba=registrar o X=IP");
						else
						{
							int dir =
								(rising & VPAD_BUTTON_RIGHT)
									? 1
									: -1;
							int idx = (g_menu_sel + dir +
								   count) %
								  count;
							if(apply_console(idx))
							{
								logprintf(
									"Elegida consola %d/%d: '%s' (%s)",
									idx + 1, count,
									g_config.consoles[idx]
											.name[0]
										? g_config
											  .consoles[idx]
											  .name
										: "?",
									g_config.consoles[idx]
										.host);
								// La IP cambio: el estado
								// visto antes no es de esta
								// consola; limpiar y re-consultar.
								chiaki_mutex_lock(
									&g_host_mutex);
								g_host_found = false;
								g_host_state =
									CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN;
								g_host_addr[0] = 0;
								chiaki_mutex_unlock(
									&g_host_mutex);
								restart_discovery();
							}
						}
					}
				}
				else if(rising & (VPAD_BUTTON_ZL |
						  VPAD_BUTTON_ZR))
				{
					// ZL/ZR: perfil de calidad (fase
					// 4c). Solo fuera de sesion: se
					// aplica al reconectar (el perfil
					// se negocia al abrir stream).
					if(g_session_active)
						logprintf(
							"ZL/ZR: en sesion (B para parar)");
					else
					{
						int cur =
							video_preset_cur();
						int dir = (rising &
							   VPAD_BUTTON_ZR)
								 ? 1
								 : -1;
						g_config.video_preset =
							(cur + dir +
							 VIDEO_PRESET_COUNT) %
							VIDEO_PRESET_COUNT;
						g_config_dirty = true;
						logprintf(
							"Calidad: %s (aplica al reconectar)",
							video_preset_label(
								g_config
									.video_preset));
					}
				}
			}
		}

		// Toques de sesion: dos zonas en el DRC (la unica pantalla
		// con tactil). La esquina superior derecha es la superficie
		// touchpad de la PS4 (arrastre = deslizar, toque corto =
		// click); la inferior derecha es el icono de parada. El
		// resto de la pantalla se ignora.
		if(error == VPAD_READ_SUCCESS && g_session_active &&
		   !g_swkbd_visible)
		{
			VPADTouchData tp = {};
			VPADGetTPCalibratedPointEx(VPAD_CHAN_0, VPAD_TP_854X480,
						 &tp, &status.tpNormal);
			bool touched = tp.touched != 0;

			// Diagnostico unico por sesion: el primer contacto en
			// cualquier punto deja crudo y calibrado en el log.
			if(touched && !g_tp_diag)
			{
				g_tp_diag = true;
				logprintf("Touch: crudo(%u,%u) -> cal854(%u,%u)",
					  (unsigned)status.tpNormal.x,
					  (unsigned)status.tpNormal.y,
					  (unsigned)tp.x, (unsigned)tp.y);
			}

			// --- Zona touchpad (esquina superior derecha) ---
			int zx, zy, zw, zh;
			touchpad_rect(854, 480, &zx, &zy, &zw, &zh);
			if(g_tp_down)
			{
				if(touched)
				{
					// Arrastre: clamp a la zona y mapeo lineal
					// al pad completo (1920x1079).
					int lx = (int)tp.x - zx, ly = (int)tp.y - zy;
					if(lx < 0)
						lx = 0;
					else if(lx >= zw)
						lx = zw - 1;
					if(ly < 0)
						ly = 0;
					else if(ly >= zh)
						ly = zh - 1;
					if(lx - g_tp_lx0 > 12 || g_tp_lx0 - lx > 12 ||
					   ly - g_tp_ly0 > 12 || g_tp_ly0 - ly > 12)
						g_tp_moved = true;
					// Antitemblor + inercia: con el dedo quieto
					// la pos no cambia (banda de TP_JITTER_PX)
					// y la PS4 ve el dedo parado. En movimiento
					// el punto estable no salta al crudo (que
					// tiembla y se ve erratico): lo sigue con
					// inercia (0.3 de la distancia por
					// frame); los gestos rapidos (>12 px por
					// frame) van directos para no perder
					// precision.
					float ex = (float)lx - g_tp_ax;
					float ey = (float)ly - g_tp_ay;
					if(ex > TP_JITTER_PX || -ex > TP_JITTER_PX ||
					   ey > TP_JITTER_PX || -ey > TP_JITTER_PX)
					{
						if(ex > 12.0f || ex < -12.0f ||
						   ey > 12.0f || ey < -12.0f)
						{
							g_tp_ax = (float)lx;
							g_tp_ay = (float)ly;
						}
						else
						{
							g_tp_ax += ex * 0.3f;
							g_tp_ay += ey * 0.3f;
						}
						unsigned nx =
							(unsigned)(int)(g_tp_ax + 0.5f);
						unsigned ny =
							(unsigned)(int)(g_tp_ay + 0.5f);
						g_tp_x = (uint16_t)(nx * PS_TP_MAXX /
							(unsigned)(zw - 1));
						g_tp_y = (uint16_t)(ny * PS_TP_MAXY /
							(unsigned)(zh - 1));
					}
				}
				else
				{
					// Levantado: toque corto y quieto = click
					// del touchpad (pulso tras subir el dedo).
					g_tp_down = false;
					if(!g_tp_moved &&
					   OSGetTime() - g_tp_down_ticks <
						   OSMillisecondsToTicks(300))
					{
						g_tp_click_ticks =
							OSGetTime() +
							OSMillisecondsToTicks(150);
						logprintf("Touchpad: click");
					}
				}
			}
			else if(touched && (int)tp.x >= zx &&
				(int)tp.x < zx + zw && (int)tp.y >= zy &&
				(int)tp.y < zy + zh)
			{
				// Contacto nuevo dentro de la zona.
				int lx = (int)tp.x - zx, ly = (int)tp.y - zy;
				g_tp_down = true;
				g_tp_moved = false;
				g_tp_lx0 = lx;
				g_tp_ly0 = ly;
				g_tp_ax = (float)lx; // punto estable de partida
				g_tp_ay = (float)ly;
				g_tp_id = g_tp_id_next;
				g_tp_id_next =
					(uint8_t)((g_tp_id_next + 1) & 0x7f);
				g_tp_down_ticks = OSGetTime();
				g_tp_click_ticks = 0; // cancela un pulso previo
				g_tp_x = (uint16_t)((unsigned)lx * PS_TP_MAXX /
						     (unsigned)(zw - 1));
				g_tp_y = (uint16_t)((unsigned)ly * PS_TP_MAXY /
						     (unsigned)(zh - 1));
			}

			// Tocar la zona re-muestra el surface: el contacto lo
			// mantiene visible y al soltar se va2 s despues.
			if(g_tp_down)
				g_tp_show_until =
					OSGetTime() + OSMillisecondsToTicks(2000);

			// --- Icono de parada (esquina inferior derecha) ---
			// Solo sin contacto del touchpad en curso: un arrastre
			// que pase por abajo no debe parar la sesion.
			if(!g_tp_down)
			{
				int sx, sy, sd;
				stop_button_rect(854, 480, &sx, &sy, &sd);
				const int pad = 16;
				bool inside =
					tp.touched && (int)tp.x >= sx - pad &&
					(int)tp.x < sx + sd + pad &&
					(int)tp.y >= sy - pad &&
					(int)tp.y < sy + sd + pad;
				if(inside && !stop_touch_prev)
				{
					logprintf(
						"Stop: toque en el icono off (%u,%u)",
						(unsigned)tp.x,
						(unsigned)tp.y);
					stop_session();
				}
				stop_touch_prev = inside;
			}
			else
				stop_touch_prev = false;
		}
		else
		{
			// Sin sesion o con teclado abierto: reinicia todo para
			// que el proximo toque cuente.
			stop_touch_prev = false;
			g_tp_down = false;
			g_tp_diag = false;
		}

		// VPAD -> sesion (fase 3c): estado fresco cada frame; la
		// cadencia de red la decide el feedback sender de chiaki. Con
		// el teclado del sistema abierto se manda reposo para que
		// ningun boton quede pegado mientras se teclea.
		if(error == VPAD_READ_SUCCESS && g_session_active)
		{
			if(g_swkbd_visible)
				session_send_idle();
			else
				session_send_input(status);
		}

		// Estadisticas de video (valida que FEC entrega frames enteros).
		if(g_session_active)
		{
			uint64_t now = OSGetTime();
			uint64_t frames = g_video_frames;
			// Relleno del ring AX por detras del playhead del DSP.
			audio_update();
			// Inicio del stream: el surface del touchpad aparece
			// transparente2 s para situarlo sin tapar el juego.
			if(!g_tp_stream_seen && frames > 0)
			{
				g_tp_stream_seen = true;
				g_tp_show_until =
					now + OSMillisecondsToTicks(2000);
			}
			// Detector de congelados (wifi 2.4 GHz): frames
			// entrando pero ninguno decodificable => las
			// referencias H.264 se perdieron en una rafaga. Pide
			// keyframe al instante (reintento cada 1.2 s) en vez
			// de esperar ~5 s al IDR periodico de la PS4.
			{
				WiiUVideoStats vs = wiiu_video_stats();
				if(frames != g_stall_fed)
				{
					g_stall_fed = frames;
					g_stall_last_fed_ticks = now;
				}
				if(vs.decoded != g_stall_decoded)
				{
					// Si habia un IDR pedido, aqui esta la
					// recuperacion: se registra cuanto tardo.
					if(g_stall_idr_sent_ticks)
					{
						logprintf(
							"Sesion: decoder recuperado "
							"tras IDR en %llu ms",
							(unsigned long long)(
								(now - g_stall_idr_sent_ticks) /
								OSMillisecondsToTicks(1)));
						g_stall_idr_sent_ticks = 0;
					}
					g_stall_decoded = vs.decoded;
					g_stall_last_dec_ticks = now;
				}
				// Armado tras el primer frame decodificado: durante
				// el arranque de la sesion (decoder subiendose, PS4
				// mandando su primer IDR) todavia no hay nada que
				// "estancar" — el detector de gui-38 disparaba un
				// falso positivo a los 2 s con una peticion inutil.
				if(frames > 0 &&
				   vs.decoded > 0 &&
				   now - g_stall_last_fed_ticks <
					   OSMillisecondsToTicks(500) &&
				   now - g_stall_last_dec_ticks >
					   OSMillisecondsToTicks(700) &&
				   now - g_stall_last_idr_ticks >
					   OSMillisecondsToTicks(1200))
				{
					uint64_t stall_ms =
						(now - g_stall_last_dec_ticks) /
						OSMillisecondsToTicks(1);
					chiaki_session_request_idr(&g_session);
					g_stall_last_idr_ticks = now;
					g_stall_last_dec_ticks = now;
					g_stall_idr_sent_ticks = now;
					logprintf("Sesion: decoder estancado "
							  "%llu ms — pidiendo "
							  "keyframe (IDR)",
							  (unsigned long long)stall_ms);
				}
			}
			if(now - g_video_log_ticks > OSSecondsToTicks(3))
			{
				if(frames != g_video_log_frames)
				{
					WiiUVideoStats vs = wiiu_video_stats();
					// g_video_log_presented se queda con
					// la sesion anterior: si vs.presented
					// volvio a 0 (reset por sesion) el
					// delta rebose hacia atras y salia
					// "+6148914691236517071/s". Se trata
					// como reinicio (delta 0).
					const unsigned long long dp =
						(vs.presented >=
						 g_video_log_presented)
							? (unsigned long long)(
								  vs.presented -
								  g_video_log_presented)
							: 0;
					logprintf(
						"Video: %llu frames (%.2f MB) perdidos=%llu "
						"FEC_fallos=%llu | hw_dec=%llu err=%llu drop=%llu "
						"exec=%uus lock=%uus mem=%uus | present=%llu "
						"(+%llu/s) upload=%uus age=%uus loop=%llu (+%llu/s)",
						(unsigned long long)frames,
						(double)g_video_bytes / (1024.0 * 1024.0),
						(unsigned long long)g_video_frames_lost,
						(unsigned long long)g_fec_failures,
						(unsigned long long)vs.decoded,
						(unsigned long long)vs.errors,
						(unsigned long long)vs.dropped,
						(unsigned int)vs.exec_us,
						(unsigned int)vs.lock_us,
						(unsigned int)vs.mem_us,
						(unsigned long long)vs.presented,
						(unsigned long long)(dp / 3),
						(unsigned int)vs.upload_us,
						(unsigned int)vs.age_us,
						(unsigned long long)g_main_iters,
						(unsigned long long)((g_main_iters -
								      g_video_log_iters) /
							     3));
					g_video_log_frames = frames;
					g_video_log_presented = vs.presented;
					g_video_log_iters = g_main_iters;
				}
				g_video_log_ticks = now;
			}
		}

		// Patron oficial (wut samples/gx2_triangle): el clear DEBE ir dentro
		// del pass de cada pantalla y copiarse al scan buffer con Finish*TV/DRC;
		// sin eso WHBGfxClearColor solo limpia el DRC (sDrawingTv arranca en
		// FALSE) y la pantalla queda negra.
		// Sesion activa: verde pulsante (senal de stream). Menu: clear
		// #121212, el MISMO gris del fondo del menu — antes era el
		// "azul discovery" pulsante y se veia como un brillo azul en
		// los primeros frames hasta que el frame se asentaba.
		uint64_t ticks = OSGetTime();
		float pulse = 0.5f + 0.5f * (float)((ticks >> 24) & 0xff) / 255.0f;
		float clear_r = 0.07f, clear_g = 0.07f, clear_b = 0.07f;
		if(g_session_active)
		{
			clear_r = 0.05f;
			clear_g = 0.15f + 0.35f * pulse;
			clear_b = 0.1f;
		}

		// Subida del frame de video UNA vez por vuelta y FUERA de
		// los pases WHBGfx: los passes solo dibujan el par ya
		// subido (gui-40 subia dentro de cada pass => 2 copias
		// por vuelta, ~15.6 ms y bucle a 21 Hz).
		if(g_session_active)
			wiiu_video_upload();

		WHBGfxBeginRender();

		WHBGfxBeginRenderTV();
		WHBGfxClearColor(clear_r, clear_g, clear_b, 1.0f);
		if(g_session_active)
		{
			wiiu_video_render(1920, 1080);
			// Sin muestra de video aun: tarjeta "Conectando".
			if(g_video_frames == 0)
				draw_connecting(1920, 1080);
			draw_stop_button(1920, 1080);
			// El surface del touchpad es solo en el DRC: la TV no
			// tiene tactil y no debe tapar el juego.
		}
		else
			draw_menu(1920, 1080);
		if(g_swkbd_visible)
			nn::swkbd::DrawTV();
		WHBGfxFinishRenderTV();

		WHBGfxBeginRenderDRC();
		WHBGfxClearColor(clear_r, clear_g, clear_b, 1.0f);
		if(g_session_active)
		{
			wiiu_video_render(854, 480);
			if(g_video_frames == 0)
				draw_connecting(854, 480);
			draw_stop_button(854, 480);
			draw_touchpad(854, 480);
		}
		else
			draw_menu(854, 480);
		if(g_swkbd_visible)
			nn::swkbd::DrawDRC();
		WHBGfxFinishRenderDRC();

		WHBGfxFinishRender();
	}

	logprintf("chiaki-ng Wii U: shutting down");

	// Orden inverso al arranque: sesion -> registro -> discovery -> teclado
	// -> graficos -> log -> red.
	if(g_session_active)
		stop_session(true);
	// Sin sesion activa no hay decoder, pero por si acaso (idempotente).
	// wait_gpu=false: en el camino de shutdown GX2DrawDone puede DSI
	// (helper de gx2 con el contexto en background) y el crash handler
	// de wut suspende el hilo principal = consola congelada al volver
	// al menu HOME tras transmitir.
	wiiu_video_reset(false);
	if(g_regist_state == RegistState::Running)
		chiaki_regist_stop(&g_regist);
	if(g_regist_state != RegistState::Idle)
	{
		chiaki_regist_fini(&g_regist);
		g_regist_state = RegistState::Idle;
	}
	stop_discovery();
	chiaki_mutex_fini(&g_host_mutex);
	log_file_close();
	swkbd_shutdown();
	FSShutdown();
	VPADShutdown();
	audio_quit();
	AXQuit();
	WHBGfxShutdown();
	// Cerrar el socket UDP de log ANTES de desmontar la red de Cafe OS.
	WHBLogUdpDeinit();
	WHBLogCafeDeinit();
	socket_lib_finish();
	WHBProcShutdown();
	return 0;
}
