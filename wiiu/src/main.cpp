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
 *   FEC). El Account ID de PSN se introduce a mano (16 hex).
 *   Sin decoder aun: los frames de video se descartan y se contabilizan
 *   (valida FEC/gf-complete en la ruta real); el decode llega en fase 3.
 *
 * Controles:
 *   X = configurar IP de la consola (teclado del sistema)
 *   Y = borrar IP y volver a broadcast 255.255.255.255
 *   ↑ = registrar consola (ID PSN + PIN de "Enlazar con otro dispositivo")
 *   A = conectar Remote Play (despierta la PS4 si esta en standby)
 *   B = parar la sesion activa
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

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// Registro en curso (chiaki_regist lanza su propio thread).
enum class RegistState { Idle, Running, Finished };
static ChiakiRegist g_regist;
static RegistState g_regist_state = RegistState::Idle;

// Sesion de Remote Play (chiaki_session lanza su propio thread).
static ChiakiSession g_session;
static bool g_session_active = false;
static volatile bool g_session_quit_received = false;
static volatile bool g_session_login_pin_requested = false;
static volatile bool g_session_login_pin_incorrect = false;
static volatile uint64_t g_video_frames = 0;
static volatile uint64_t g_video_bytes = 0;
static volatile uint64_t g_video_frames_lost = 0;
static volatile uint64_t g_fec_failures = 0;
static uint64_t g_video_log_ticks = 0;
static uint64_t g_video_log_frames = 0;

// Teclado del sistema (nn::swkbd)
enum class Prompt { None, IP, PsnId, AccountId, Pin, LoginPin };
static FSClient *g_fs_client = nullptr;
static void *g_swkbd_work_mem = nullptr;
static bool g_swkbd_created = false;
static bool g_swkbd_visible = false;
static Prompt g_prompt = Prompt::None;
static Prompt g_swkbd_reopen = Prompt::None;
static char16_t g_swkbd_initial[40];
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

// Buffer estatico alineado a 64 (mismo criterio que wut usa en su file.c
// para los buffers que cruzan IPC hacia el modulo FS). Estatico para no
// comer la pila del hilo de 4 KB del crash handler.
#define LOG_LINE_SIZE 0x1200
static char g_log_line[LOG_LINE_SIZE] __attribute__((aligned(64)));

// Cola de respaldo para quien no puede escribir; la vacia el duenio del
// mutex. Ambos bloqueos son TryLock: el logger jamas se bloquea en algo
// que no sea su propia escritura FS (un hilo muerto con el mutex no puede
// hundir al resto; si la cola esta ocupada, la linea se pierde aqui pero
// ya esta en UDP/OSReport).
#define LOGQ_LINES 32
#define LOGQ_SIZE 640
static char g_logq[LOGQ_LINES][LOGQ_SIZE] __attribute__((aligned(64)));
static uint32_t g_logq_head = 0; // proxima a vaciar (duenio del mutex)
static uint32_t g_logq_tail = 0; // proxima a encolar (el que no pudo)

// Formatea "[t] str\r\n" (con NUL final); devuelve los bytes utiles.
static int log_line_format(char *dst, size_t dst_size, const char *str)
{
	uint64_t ms = OSGetTime() / 125000; // OS_TIMER_CLOCK = 125 MHz
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

// Vuelca la cola pendiente; solo el duenio de g_log_mutex. TryLock en la
// cola: si alguien esta encolando en este instante, lo dejamos para la
// proxima linea (nada se bloquea).
static void logq_drain(void)
{
	for(;;)
	{
		if(!OSTryLockMutex(&g_logq_mutex))
			return;
		if(g_logq_head == g_logq_tail)
		{
			OSUnlockMutex(&g_logq_mutex);
			return;
		}
		uint32_t slot = g_logq_head % LOGQ_LINES;
		char tmp[LOGQ_SIZE] __attribute__((aligned(64)));
		int len = (int)strlen(g_logq[slot]);
		memcpy(tmp, g_logq[slot], (size_t)len + 1);
		g_logq_head++;
		OSUnlockMutex(&g_logq_mutex);
		log_line_write(tmp, len);
	}
}

static void log_sd_handler(const char *str)
{
	if(!g_log_ready)
		return;

	if(OSTryLockMutex(&g_log_mutex))
	{
		int n = log_line_format(g_log_line, sizeof(g_log_line), str);
		log_line_write(g_log_line, n);
		logq_drain();
		OSUnlockMutex(&g_log_mutex);
		return;
	}

	// Otro hilo esta escribiendo (o murio reteniendo el mutex): encolar
	// y ya. Nunca FS desde aqui — es lo que colgaba el hilo regist.
	if(!OSTryLockMutex(&g_logq_mutex))
		return; // cola ocupada un microsegundo: UDP ya la tiene
	char line[LOGQ_SIZE] __attribute__((aligned(64)));
	int n = log_line_format(line, sizeof(line), str);
	if(g_logq_tail - g_logq_head >= LOGQ_LINES)
		g_logq_head++; // llena: descartamos la mas vieja
	memcpy(g_logq[g_logq_tail % LOGQ_LINES], line, (size_t)n + 1);
	g_logq_tail++;
	OSUnlockMutex(&g_logq_mutex);
}

static void logprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logprintf(const char *fmt, ...)
{
	char msg[400];
	va_list va;
	va_start(va, fmt);
	vsnprintf(msg, sizeof(msg), fmt, va);
	va_end(va);
	WHBLogWrite(msg); // handlers: cafe OSReport + UDP 4405 + SD (handler)
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
	// Unico escritor FS: ahora podemos bloquear con tranquilidad.
	OSLockMutex(&g_log_mutex);
	logq_drain();
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
static void wiiu_chiaki_log_cb(ChiakiLogLevel level, const char *msg, void *user)
{
	(void)user;
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
		appearArg.inputFormArg.hintText = u"Account ID PSN: 16 hex (psn.flipscreen.games)";
		appearArg.inputFormArg.maxTextLength = 16;
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
		}
		chiaki_mutex_unlock(&g_host_mutex);
		if(rh)
			logprintf("REGISTRO OK: consola '%s' clave=%.16s", rh->server_nickname,
				     rh->rp_regist_key);
		logprintf("Clave solo en RAM — pulsa A para conectar Remote Play");
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
	(void)buf;
	(void)user;
	(void)frame_recovered;
	g_video_frames++;
	g_video_bytes += buf_size;
	if(frames_lost > 0)
		g_video_frames_lost += (uint64_t)frames_lost;
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
		g_session_quit_received = true;
		break;
	default:
		break;
	}
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
	chiaki_connect_video_profile_preset(&info.video_profile,
					     CHIAKI_VIDEO_RESOLUTION_PRESET_720p,
					     CHIAKI_VIDEO_FPS_PRESET_60);
	info.video_profile_auto_downgrade = true;
	info.enable_keyboard = false;
	info.enable_dualsense = false;
	info.audio_video_disabled = CHIAKI_NONE_DISABLED;
	info.auto_regist = false;
	info.packet_loss_max = 0.05; // tope de perdida reportada al servidor
	info.enable_idr_on_fec_failure = true;
	memcpy(info.psn_account_id, g_psn_account_id, sizeof(info.psn_account_id));

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
	// Mando en reposo: la fase 4 conectara VPAD; aqui validamos el canal.
	ChiakiControllerState idle_state;
	chiaki_controller_state_set_idle(&idle_state);
	chiaki_session_set_controller_state(&g_session, &idle_state);

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
	g_video_bytes = 0;
	g_video_frames_lost = 0;
	g_fec_failures = 0;
	g_video_log_ticks = OSGetTime();
	g_video_log_frames = 0;
	logprintf("Sesion: thread en marcha (↑ registra | A conecta | B para)");
	return true;
}

static void stop_session()
{
	if(!g_session_active)
		return;
	logprintf("Sesion: parando...");
	chiaki_session_stop(&g_session);
	chiaki_session_join(&g_session);
	chiaki_session_fini(&g_session);
	g_session_active = false;
	g_session_login_pin_requested = false;
	logprintf("Sesion finalizada");
}

static void handle_press_a()
{
	if(g_session_active)
	{
		logprintf("A: ya hay una sesion activa (B para parar)");
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
		logprintf("↑: introduce el Account ID de PSN (16 hex; en psn.flipscreen.games)");
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
				logprintf("Account ID invalido: '%s' — 16 hex o base64", input);
				g_swkbd_reopen = Prompt::AccountId;
				break;
			}
			g_have_psn_account = true;
			char hex[17] = { 0 };
			for(size_t i = 0; i < sizeof(g_psn_account_id); i++)
				snprintf(hex + i * 2, 3, "%02x", g_psn_account_id[i]);
			logprintf("Account ID guardado: %s", hex);
			logprintf("Introduce el PIN de 8 digitos de la PS4");
			g_swkbd_reopen = Prompt::Pin;
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
			logprintf("Registro cancelado (sin Account ID)");
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
	logprintf("init: AXInit");
	AXInit();

	// Teclado del sistema (IP, PSN, PINs).
	logprintf("init: swkbd");
	swkbd_init();

	// Log a la SD: fs:/vol/external01/wiiu/apps/chiaki/log.txt
	logprintf("init: log SD");
	log_file_init();

	// Tras log_file_init para que el marker llegue a la SD (antes solo
	// iba a OSReport/UDP y no servia para confirmar el build desplegado).
	logprintf("BUILD wiiu-2026-10-15 rcvbuf-32k-final");

	// Requiere socket_lib_init ya hecho (crea sockets y threads).
	logprintf("init: discovery");
	g_discovery_running = start_discovery(nullptr);

	logprintf("Controles: X=IP  Y=broadcast  Arriba=registrar  A=conectar  B=parar sesion");

	uint32_t last_hold = 0;
	VPADTouchData last_touch = {};

	while(WHBProcIsRunning())
	{
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
				log_vpad_changes(status, last_hold, last_touch);

				if(rising & VPAD_BUTTON_X)
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
			}
		}

		// Estadisticas de video (valida que FEC entrega frames enteros).
		if(g_session_active)
		{
			uint64_t now = OSGetTime();
			uint64_t frames = g_video_frames;
			if(now - g_video_log_ticks > OSSecondsToTicks(3))
			{
				if(frames != g_video_log_frames)
				{
					logprintf(
						"Video: %llu frames (%.2f MB) perdidos=%llu "
						"FEC_fallos=%llu",
						(unsigned long long)frames,
						(double)g_video_bytes / (1024.0 * 1024.0),
						(unsigned long long)g_video_frames_lost,
						(unsigned long long)g_fec_failures);
					g_video_log_frames = frames;
				}
				g_video_log_ticks = now;
			}
		}

		// Patron oficial (wut samples/gx2_triangle): el clear DEBE ir dentro
		// del pass de cada pantalla y copiarse al scan buffer con Finish*TV/DRC;
		// sin eso WHBGfxClearColor solo limpia el DRC (sDrawingTv arranca en
		// FALSE) y la pantalla queda negra.
		uint64_t ticks = OSGetTime();
		float pulse = 0.5f + 0.5f * (float)((ticks >> 24) & 0xff) / 255.0f;

		WHBGfxBeginRender();

		// Verde pulsante = sesion de Remote Play activa; azul = discovery.
		float base_g = g_session_active ? 0.15f + 0.35f * pulse : 0.2f;
		float base_b = g_session_active ? 0.1f : pulse;

		WHBGfxBeginRenderTV();
		WHBGfxClearColor(g_session_active ? 0.05f : 0.1f, base_g, base_b, 1.0f);
		if(g_swkbd_visible)
			nn::swkbd::DrawTV();
		WHBGfxFinishRenderTV();

		WHBGfxBeginRenderDRC();
		WHBGfxClearColor(g_session_active ? 0.05f : 0.1f, base_g, base_b, 1.0f);
		if(g_swkbd_visible)
			nn::swkbd::DrawDRC();
		WHBGfxFinishRenderDRC();

		WHBGfxFinishRender();
	}

	logprintf("chiaki-ng Wii U: shutting down");

	// Orden inverso al arranque: sesion -> registro -> discovery -> teclado
	// -> graficos -> log -> red.
	if(g_session_active)
		stop_session();
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
	AXQuit();
	WHBGfxShutdown();
	// Cerrar el socket UDP de log ANTES de desmontar la red de Cafe OS.
	WHBLogUdpDeinit();
	WHBLogCafeDeinit();
	socket_lib_finish();
	WHBProcShutdown();
	return 0;
}
