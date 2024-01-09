#ifndef SSIDHELPER_H
#define SSIDHELPER_H

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include "discoverymanager.h"

/**
 * \brief This is a helper namespace for the Autoconnect CLI command.
 * The methods for autoconnect are different if accessing locally or remotely
 * - For local, we use the discovery_manager to find the IP of the console via UDP
 * - For remote, we use the CLI to connect the same way the old gen_launcher script did.
 */
namespace AutoConnectHelper {

    /**
     * \brief Get the current Executable as a QString. Should handle flatpak and appimage for Linux
     * \return QString of the command to run the current executable
     */
    static QString GetExecutable() {
        #if defined(__APPLE__)
            char buffer[PATH_MAX];
            uint32_t size = sizeof(buffer);

            if (_NSGetExecutablePath(buffer, &size) == -1) {
                // Buffer is not large enough, allocate a larger one
                auto largerBuffer = new char[size];
                if (_NSGetExecutablePath(largerBuffer, &size) == 0) {
                    return largerBuffer;
                }
            } else {
                return buffer;
            }
        #elif defined(__linux)
            //Check if we're a flatpak or appimage
            const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            const QString flatpakId = env.value("FLATPAK_ID");
            QString appImagePath = env.value("APPIMAGE");
            if (!flatpakId.isEmpty()) {
                return QString("flatpak run %1").arg(flatpakId);
            } else if (!appImagePath.isEmpty()) {
                return appImagePath;
            } else {
                char buffer[PATH_MAX];
                ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);

                if (len != -1) {
                    buffer[len] = '\0';
                    return QString::fromUtf8(buffer);
                }
            }
        #endif
        return "";
    }

    /**
     * \brief Run a shell command and get the stdout response as a QString
     * \param command The command to run (or CLI commad)
     * \param prependExecutable if true, will run the command as arguments to the CLI
     * \return QString of stdout
     */
    static QString RunShellCommand(QString command, bool prependExecutable = true) {
        if (prependExecutable) {
            command = QString("%1 %2").arg(GetExecutable()).arg(command);
        }

        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.toStdString().c_str(), "r"), pclose);
        if (!pipe) {
            fprintf(stderr, "popen() failed for command %s\n", command.toStdString().c_str());
            throw std::runtime_error("popen() failed!");
        }
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }

        return QString::fromStdString(result);
    }

    /**
     * \brief Use OS dependant system tools to get the current SSID.
     * \return QString of the currently connected SSID.
     */
    static QString GetCurrentSSID(){
        std::string command;
        #if defined(__APPLE__)
            command = "/System/Library/PrivateFrameworks/Apple80211.framework/Resources/airport -I | grep SSID | grep -v BSSID | awk -F: '{print $2}'";
        #elif defined(__linux__)
            command = "iwgetid -r";
        #elif defined(_WIN32)
            command = "netsh wlan show interfaces | findstr /R \"^\\s*SSID\"";
        #else
            #error "Unsupported platform"
        #endif

        QString result = RunShellCommand(QString::fromStdString(command), false);

        // Remove newline characters
        result.remove(QChar('\n'));
        result.remove(QChar('\r'));

        return result;
    }

    static void CheckDiscover(const bool local, const DiscoveryManager& discovery_manager, QString& host, bool& needsWaking, bool& discovered, const QString& mac = "") {
        if (local) {
            for(const auto &registered_host : discovery_manager.GetHosts()) {
                if (registered_host.GetHostMAC().ToString() == mac) {
                    host = registered_host.host_addr;
                    needsWaking = registered_host.state != CHIAKI_DISCOVERY_HOST_STATE_READY;
                    discovered = true;
                    break;
                }
            }
        } else {
            const QString discoverResponse = RunShellCommand(QString("discover --host=%1").arg(host));
            discovered = discoverResponse.contains("ready") || discoverResponse.contains("standby");
            needsWaking = !discoverResponse.contains("ready");
        }
    }

    static void CheckReady(const bool local, const DiscoveryManager& discovery_manager, const QString& host, bool& ready, const QString& mac = "") {
        if (local) {
            for(const auto &registered_host : discovery_manager.GetHosts()) {
                if (registered_host.GetHostMAC().ToString() == mac) {
                    ready = true;
                    break;
                }
            }
        } else {
            const QString discoverResponse = RunShellCommand(QString("discover --host=%1").arg(host));
            ready = discoverResponse.contains("ready");
        }
    }

    static void SendWakeup(const bool local, DiscoveryManager& discovery_manager, const QString& host, const QByteArray& regist_key, const bool& isPS5) {
        if (local) {
            discovery_manager.SendWakeup(host, regist_key, isPS5);
        } else {
            RunShellCommand(QString("wakeup -%1 -h %2 -r '%3' 2>/dev/null")
            	.arg(isPS5 ? "5" : "4")
            	.arg(host)
            	.arg(regist_key));
        }
    }
}

#endif //SSIDHELPER_H
