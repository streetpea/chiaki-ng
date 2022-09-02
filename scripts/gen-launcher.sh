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

# If it's still not up after 20 seconds, something has gone wrong.
wait_timeout=${wait_timeout:-20}

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
            echo -e "Option ${REPLY}: ${nickname} was chosen"
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

while true
do
    read -n1 -p $'Enter Your Playstation Console (4 or 5):\x0a' ps_console
    echo
    if [ "${ps_console}" != "4" ] && [ "${ps_console}" != "5" ]
    then
        echo "PlayStation ${ps_console} is not valid, please use either 4 or 5."
        echo
    else 
        break
    fi
done

while true
do
    read -e -p $'Enter your PlayStation IP (form should be xxx.xxx.xxx.xxx like 192.168.1.16):\x0a' ps_ip
    # ip route fine here because you need to be able to connect to PS IP for remote play, and if you can't you have override option later
    if ip_validator "${ps_ip}" &>/dev/null
    then
        break
    else
        echo "IP address not valid: ${ps_ip}. Please re-enter you IP" >&2
    fi
done
echo
while true
do
    read -e -p $'Choose your default mode\x0afullscreen [black bars, aspect ratio maintained]\x0azoom [no black bars, aspect ratio maintained, edges of image cut off by zoom]\x0astretch [no black bars, aspect ratio slightly distorted]):\x0a' mode
    echo
    if [ "${mode}" != "fullscreen" ] && [ "${mode}" != "zoom" ] && [ "${mode}" != "stretch" ]
    then
        echo "Mode: ${mode} is not valid, please use fullscreen, zoom, or stretch."
        echo
    else 
        break;
    fi
done

# Create script
cat <<EOF > "$config_path/Chiaki-launcher.sh"
#!/usr/bin/env bash
set -Eeo pipefail

connect_error()
{
    echo "Error: Couldn't connect to PlayStation console!" >&2
    echo "Please check that your Steam Deck and PlayStation are on the same network and you have the right PlayStation IP Address!" >&2
    exit 1
}

# Print connect error message and exit if a failure occurs connecting to the console.
trap connect_error ERR

# Wake up console from sleep/rest mode (must be either on or in sleep/rest mode for this to work)
flatpak run re.chiaki.Chiaki4deck wakeup -${ps_console} -h ${ps_ip} -r '${regist_key}'
sleep 1
# wait for PlayStation to return one successful packet, exit script on error if it never happens
ping -c 1 -w ${wait_timeout} ${ps_ip} &>/dev/null
sleep 1

# Chiaki handles its own errors, so unsetting trap here.
trap - ERR

# Begin playing PlayStation remote play via Chiaki on your Steam Deck :)
flatpak run re.chiaki.Chiaki4deck --${mode} stream '${server_nickname}' ${ps_ip}
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


