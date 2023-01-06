#!/usr/bin/env bash

# set exit on error
set -Eeo pipefail

# Allow users to override preset variables on command line via exports 
#(i.e. export config_path=${HOME}/my_fav_path) before running script
config_path=${config_path:-"${HOME}/.var/app/re.chiaki.Chiaki4deck/config/Chiaki"}
config_file=${config_file:-"${config_path}/Chiaki.conf"}
if ! [ -f "${config_file}" ]
then
    echo "Config file: ${config_file} does not exist" >&2
    echo "Please run your flatpak at least once with: flatpak run re.chiaki.Chiaki4deck and try again" >&2
    exit 2
fi
if ! grep regist_key < "${config_file}" &>/dev/null
then
    echo "Registration key is not set" >&2
    echo "Register your console via the GUI and try again." >&2
    exit 2
fi

# create registration key and nickname array to handle case of multiple registered consoles
# consoles get a nickname after registration so regist_array size should equal nickname_array size
readarray -t regist_array < <(grep regist_key < "${config_file}" | cut -d '(' -f2 | cut -d '\' -f1)
readarray -t nickname_array < <(grep server_nickname < "${config_file}" | cut -d '=' -f2)
consoles_registered="${#nickname_array[@]}"

# If it's still not up after 35 seconds, something has gone wrong.
wait_timeout=${wait_timeout:-35}

ip_validator()
{
    local ip=$1
    # Check the IP address against IPV4 regex (4 numbers separated by a ., 1-3 digits each)
    if [[ $ip =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]]
    then
        # Parse on .
        IFS='.'
        # Create an array of each number of the IP Address
        ip_parts=($ip)
        # check each part is a number from 0-255 and return result (0=>valid, !=0=>invalid)
        [ ${ip_parts[0]} -le 255 ] && [ ${ip_parts[1]} -le 255 ] && [ ${ip_parts[2]} -le 255 ] && [ ${ip_parts[3]} -le 255 ]
        return $?
    else
        # Didn't even get basic pattern (4 numbers separated by a ., 1-3 digits each), so definitely invalid
        return 1
    fi
}

fdqn_validator()
{
    # simple fdqn validator, could expand to add more but limited since bash doesn't support look-ahead for regex
    local hostname=$1
    [[ "${hostname}" =~ ^([a-zA-Z0-9][a-zA-Z0-9-]{0,61}[a-zA-Z0-9]\.)+[a-zA-Z]{2,}$ ]]
    return $? 
}

remove_whitespace()
{
    # removes trailing and leading whitespace using built-in bash functionality
    local string=$1
    # remove leading whitespace
    string="${string#"${string%%[![:space:]]*}"}"
    # remove trailing whitespace characters
    string="${string%"${string##*[![:space:]]}"}"
    echo "${string}"
}

select_addr()
{
    local address_type=""
    local addr=""
    PS3="NOTICE: Use 1 unless you created a hostname (FQDN) for your PlayStation"$'\n'"Please select the number corresponding to your address type: "
    select address_type in "IP" "hostname"
    do
        if [ -z "${address_type}" ]
        then
            echo -e "${REPLY} is not a valid choice.\nPlease choose a number [1-2] listed above.\n" >&2
        else
            echo -e "Option ${REPLY}: ${address_type} was chosen\n" >/dev/tty
            break
        fi
    done
    if [ "${address_type}" == "IP" ]
    then
        while true
        do
            read -e -p $'Enter your PlayStation IP (should be xxx.xxx.xxx.xxx like 192.168.1.16):\x0a' addr
            if ip_validator "${addr}" &>/dev/null
            then
                break
            else
                echo "IP address not valid: ${addr}. Please re-enter your IP" >&2
            fi
        done
    echo >/dev/tty
    else
        while true
        do
            read -e -p $'Enter your PlayStation FQDN (hostname) (should be abc.ident like foo.bar.com):\x0a' addr
            if fdqn_validator "${addr}" &>/dev/null
            then
                break
            else
                echo "FQDN (hostname) not valid: ${addr}. Please re-enter your hostname" >&2
            fi
        done
    fi
    echo "${addr}"
}

# main
# If more than 1 registered console, make users choose which one they want
PS3="Please select the number corresponding to the console you want to use: "
if [ "${consoles_registered}" -gt 1 ]
then
    select nickname in "${nickname_array[@]}"
    do
        if [ -z "${nickname}" ]
        then
            echo -e "${REPLY} is not a valid choice\n" >&2
        else
            echo -e "Option ${REPLY}: ${nickname} was chosen\n"
            index=$((${REPLY}-1))
            regist_key="${regist_array[${index}]}"
            server_nickname="${nickname_array[${index}]}"
            break
        fi
    done
else
    regist_key="${regist_array[0]}"
    server_nickname="${nickname_array[0]}"
fi

server_nickname=$(remove_whitespace "${server_nickname}")

PS3="Please select the number corresponding to your Playstation Console: "
select console in "PlayStation 4" "PlayStation 5"
do
    if [ -z "${console}" ]
    then
        echo -e "${REPLY} is not a valid choice.\nPlease choose a number [1-2] listed above.\n" >&2
    else
        echo -e "Option ${REPLY}: ${console} was chosen\n"
        if [ "${console}" == "PlayStation 4" ]
        then
            ps_console=4;
        else
            ps_console=5;
        fi
        break
    fi
done

echo -e "\n\n\nHOME ADDRESS\n-------------"
home_addr="$(select_addr)"

while true
do
    read -n1 -p $'Do you have a separate address (DNS or IP) to access this console away from home? (y/n):\x0a' response

    case "${response}" in 
        [yY] )
            if iwgetid -r
            then
                default_ssid="$(iwgetid -r)"
            else
                default_ssid="none"
                echo -e "\n\nCould not detect a default SSID.\nYou are probably not connected to WIFI.\nDefaulting to none"
            fi
            echo
            read -e -p "Enter your home SSID [hit enter for default: ${default_ssid}]: " home_ssid
            if [ -z "${home_ssid}" ]
            then
                home_ssid="${default_ssid}"
            fi
            echo -e "\n\n\nAWAY ADDRESS\n-------------"
            away_addr="$(select_addr)"
            break;;
        [nN] )
            echo
            break;;
        * ) 
            echo -e "\nInvalid response, please enter y or n\n";;
    esac
done
echo

PS3="Please select the number corresponding to the default mode you want to use: "
select mode in "fullscreen" "zoom" "stretch"
do
    if [ -z "${mode}" ]
    then
        echo -e "${REPLY} is not a valid choice.\nPlease choose a number [1-3] listed above.\n" >&2
    else
        echo -e "Option ${REPLY}: ${mode} was chosen\n"
        break
    fi
done

while true
do
    read -n1 -p $'Do you have a PlayStation Login Passcode? (y/n):\x0a' response

    case "${response}" in 
        [yY] ) 
            while true
            do
                echo
                read -e -p $'Enter your 4 digit PlayStation Login Passcode:\x0a' login_passcode
                if [[ "${login_passcode}" =~ ^[0-9]{4}$ ]]
                then
                    break
                else
                    echo -e "Login passcode: ${login_passcode} not valid (must be 4 numbers).\nPlease re-enter your login passcode" >&2
                fi
            done
            echo
            break;;
        [nN] )
            echo
            break;;
        * ) 
            echo -e "\nInvalid response, please enter y or n\n";;
    esac
done

# Create script
cat <<EOF > "$config_path/Chiaki-launcher.sh"
#!/usr/bin/env bash

connect_error_loc()
{
    echo "Error: Couldn't connect to your PlayStation console from your local address!" >&2
    echo "Error: Please check that your Steam Deck and PlayStation are on the same network" >&2
    echo "Error: ...and that you have the right PlayStation IP address or hostname!" >&2
    exit 1
}

connect_error_ext()
{
    echo "Error: Couldn't connect to your PlayStation console from your external address!" >&2
    echo "Error: Please check that you have forwarded the necessary ports on your router" >&2
    echo "Error: ...and that you have the right external PlayStation IP address or hostname!" >&2
    exit 1
}

wakeup_error()
{
    echo "Error: Couldn't wake up PlayStation console from sleep!" >&2
    echo "Error: Please make sure you are using a PlayStation ${ps_console}." >&2
    echo "Error: If not, change the wakeup call to use the number of your PlayStation console" >&2
    exit 2
}

timeout_error()
{
    echo "Error: PlayStation console didn't become ready in ${wait_timeout} seconds!" >&2
    echo "Error: Please change ${wait_timeout} to a higher number in your script if this persists." >&2
    exit 1
}
EOF
if [ -n "${away_addr}" ]
then
	cat <<-EOF >> "$config_path/Chiaki-launcher.sh"
	if [ "\$(iwgetid -r)" == "${home_ssid}" ]
	then
	    addr="${home_addr}"
	    local=true
	else
	    addr="${away_addr}"
	    local=false
	fi
	EOF
else
	cat <<-EOF >> "$config_path/Chiaki-launcher.sh"
	addr="${home_addr}"
	local=true
	EOF
fi
cat <<EOF >> "$config_path/Chiaki-launcher.sh"
SECONDS=0
# Wait for console to be in sleep/rest mode or on (otherwise console isn't available)
ps_status="\$(flatpak run re.chiaki.Chiaki4deck discover -h \${addr} 2>/dev/null)"
while ! echo "\${ps_status}" | grep -q 'ready\|standby'
do
    if [ \${SECONDS} -gt ${wait_timeout} ]
    then
        if [ "\${local}" = true ]
        then
            connect_error_loc
        else
            connect_error_ext
        fi
    fi
    sleep 1
    ps_status="\$(flatpak run re.chiaki.Chiaki4deck discover -h \${addr} 2>/dev/null)"
done

# Wake up console from sleep/rest mode if not already awake
if ! echo "\${ps_status}" | grep -q ready
then
    flatpak run re.chiaki.Chiaki4deck wakeup -${ps_console} -h \${addr} -r '${regist_key}' 2>/dev/null
fi

# Wait for PlayStation to report ready status, exit script on error if it never happens.
while ! echo "\${ps_status}" | grep -q ready
do
    if [ \${SECONDS} -gt ${wait_timeout} ]
    then
        if echo "\${ps_status}" | grep -q standby
        then
            wakeup_error
        else
            timeout_error
        fi
    fi
    sleep 1
    ps_status="\$(flatpak run re.chiaki.Chiaki4deck discover -h \${addr} 2>/dev/null)"
done

# Begin playing PlayStation remote play via Chiaki on your Steam Deck :)
flatpak run re.chiaki.Chiaki4deck --passcode "${login_passcode}" --${mode} stream $(printf %q "${server_nickname}") \${addr}
EOF

# Make script executable
chmod +x "$config_path/Chiaki-launcher.sh"

# Test script
while true
do
    read -n1 -p $'Would you like to test the newly created script? (y/n):\x0a' response

    case "${response}" in 
        [yY] ) 
            echo -e "\nsounds good, launching script now..."
            "$config_path/Chiaki-launcher.sh"
            break;;
        [nN] ) 
            echo -e "\nok, try it later with: \n$config_path/Chiaki-launcher.sh\n";
            break;;
        * ) 
            echo -e "\nInvalid response, please enter y or n\n";;
    esac
done

printf '       .__    .__        __   .__   _____     .___             __    
  ____ |  |__ |__|____  |  | _|__| /  |  |  __| _/____   ____ |  | __
_/ ___\|  |  \|  \__  \ |  |/ /  |/   |  |_/ __ |/ __ \_/ ___\|  |/ /
\  \___|   Y  \  |/ __ \|    <|  /    ^   / /_/ \  ___/\  \___|    < 
 \___  >___|  /__(____  /__|_ \__\____   |\____ |\___  >\___  >__|_ \
     \/     \/        \/     \/       |__|     \/    \/     \/     \/
                    .__        __             .___._.
  ______ ___________|__|______/  |_  ____   __| _/| |
 /  ___// ___\_  __ \  \____ \   __\/ __ \ / __ | | |
 \___ \\  \___|  | \/  |  |_> >  | \  ___// /_/ |  \|
/____  >\___  >__|  |__|   __/|__|  \___  >____ |  __
     \/     \/         |__|             \/     \/  \/
'
