# Automating `chiaki4deck` Launch

Now that you have `chiaki4deck` configured, it's time to make it wake up your PlayStation and connect to it automatically. 

!!! Info "So Why Should I Care?"

    This is especially convenient for the Steam Deck's `Game Mode` where an automated launch eliminates both the need to tap furiously at your touchscreen to launch the application and the glitches that would arise (flashes on your screen) when it would switch between the 2 open Chiaki windows (since only 1 window is opened with the automated launch).

!!! Caution "Sleep Mode Required"

    For this to work, your PlayStation needs to either be in rest/sleep mode or on (so that it is listening for the wake up signal). This means you will want to keep your PlayStation in rest/sleep mode when you are not playing (instead of off).

1. Open a `konsole` session (launch it via `Applications` by clicking the Steam icon in the bottom left and searching for it in the `All Applications` section)

2. Get your PlayStation IP

    1. Get your registered PlayStation's IP Address

        1. Launch Chiaki via your desktop or via:
            
            ``` bash
            flatpak run re.chiaki.Chiaki4deck
            ```

        2. Get your IP address via the console widget and then close the window and return to the `konsole` session

            ![IP Address](images/GetAddress.png)

3. Create script for waking PlayStation and launching Chiaki

    === "Automated Instructions (Recommended)"

        1. Run the [gen-launcher script](https://raw.githubusercontent.com/streetpea/chiaki4deck/main/scripts/gen-launcher.sh){target="_blank" rel="noopener noreferrer"} using the following command in your `konsole` and answer the prompts (you will need your IP address from step 2 above)

            ``` bash
            bash <(curl -sLo- https://raw.githubusercontent.com/streetpea/chiaki4deck/main/scripts/gen-launcher.sh)
            ```

            !!! Question "What Do the Different Modes (i.e. fullscreen [uses Normal], zoom, stretch) Look Like?"

                To see examples of the different launch modes, visit the [Updates section](../updates/done.md#3-view-modes-for-non-standard-screen-sizes){target="_blank" rel="noopener noreferrer"}

            ???- example "Example Output [click to expand me]"

                ``` bash
                Enter Your Playstation Console (4 or 5):
                5
                Enter your PlayStation IP (form should be xxx.xxx.xxx.xxx like 192.168.1.16):
                192.168.1.16

                Choose your default mode
                fullscreen [black bars, aspect ratio maintained]
                zoom [no black bars, aspect ratio maintained, edges of image cut off by zoom]
                stretch [no black bars, aspect ratio slightly distorted]):
                zoom

                Would you like to test the newly created script? (y/n):
                y
                sounds good, launching script now...
                [I] Logging to file /home/deck/.var/app/re.chiaki.Chiaki4deck/data/Chiaki/Chiaki/log/chiaki_session_2022-08-26_20-47-45-787787.log
                [I] Chiaki Version 2.1.1
                [I] Using hardware decoder "vaapi"
                [I] Controller 0 opened: "Microsoft X-Box 360 pad 0 (03000000de280000ff11000001000000)"
                [I] Starting session request for PS5
                [I] Trying to request session from 192.168.1.16:9295
                [I] Connected to 192.168.1.16:9295
                [I] Sending session request
                [I] Session request successful
                [I] OpenGL initialized with version "4.6 (Core Profile) Mesa 21.3.9 (git-78c96ae5b6)"
                [I] Starting ctrl
                [I] Ctrl connected to 192.168.1.16:9295
                [I] Sending ctrl request
                [I] Ctrl received http header as response
                [I] Ctrl received ctrl request http response
                [I] Ctrl got Server Type: 2
                [I] Ctrl connected
                [I] Ctrl received Login message: success
                [I] Ctrl received valid Session Id: 1661572067T6G6FWJVE2I4HFLJXLBJJP7KVQSBE72L6IUXSRDUOVZIQOFPSK5TKQFNJHDUA6H4
                [I] Starting Senkusha
                [I] Takion connecting (version 7)
                [I] Takion enabled Don't Fragment Bit
                [I] Takion sent init
                [W] Received Ctrl Message with unknown type 0x16
                [I] Takion received init ack with remote tag 0xb18ccf, outbound streams: 0x64, inbound streams: 0x64
                [W] offset 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  0123456789abcdef
                [W]      0 01 ff                                           ..
                [I] Takion sent cookie
                [I] Takion received cookie ack
                [I] Takion connected
                [I] Senkusha sending big
                [I] Senkusha successfully received bang
                [I] Senkusha Ping Test with count 10 starting
                [I] Senkusha enabled echo
                [I] Senkusha sending Ping 0 of test index 0
                [I] Senkusha received Pong, RTT = 4.073 ms
                [I] Senkusha sending Ping 1 of test index 0
                [I] Senkusha received Pong, RTT = 3.905 ms
                [I] Senkusha sending Ping 2 of test index 0
                [I] Senkusha received Pong, RTT = 3.618 ms
                [I] Senkusha sending Ping 3 of test index 0
                [I] Senkusha received Pong, RTT = 3.649 ms
                [I] Senkusha sending Ping 4 of test index 0
                [I] Senkusha received Pong, RTT = 4.024 ms
                [I] Senkusha sending Ping 5 of test index 0
                [I] Senkusha received Pong, RTT = 4.538 ms
                [I] Senkusha sending Ping 6 of test index 0
                [I] Senkusha received Pong, RTT = 4.150 ms
                [I] Senkusha sending Ping 7 of test index 0
                [I] Senkusha received Pong, RTT = 3.719 ms
                [I] Senkusha sending Ping 8 of test index 0
                [I] Senkusha received Pong, RTT = 4.631 ms
                [I] Senkusha sending Ping 9 of test index 0
                [I] Senkusha received Pong, RTT = 3.804 ms
                [I] Senkusha disabled echo
                [I] Senkusha determined average RTT = 4.011 ms
                [I] Senkusha starting MTU in test with min 576, max 1454, retries 3, timeout 20 ms
                [I] Senkusha MTU request 1454 (min 576, max 1454), id 1, attempt 0
                [I] Senkusha MTU 1454 success
                [I] Senkusha determined inbound MTU 1454
                [I] Senkusha starting MTU out test with min 576, max 1454, retries 3, timeout 20 ms
                [I] Senkusha sent initial client MTU command
                [I] Senkusha received expected Client MTU Command
                [I] Senkusha MTU 1454 out ping attempt 0
                [I] Senkusha MTU ping 1454 success
                [I] Senkusha determined outbound MTU 1454
                [I] Senkusha sending final Client MTU Command
                [I] Senkusha is disconnecting
                [I] Senkusha closed takion
                [I] Senkusha completed successfully
                [I] Takion connecting (version 12)
                [I] Takion sent init
                [I] Takion received init ack with remote tag 0x7033129, outbound streams: 0x64, inbound streams: 0x64
                [I] Takion sent cookie
                [I] Takion received cookie ack
                [I] Takion connected
                [I] StreamConnection sending big
                [I] BANG received
                [I] [I] StreamConnection successfully received bang
                Crypt has become available. Re-checking MACs of 0 packets
                [W] Received Ctrl Message with unknown type 0x41
                [D] StreamConnection received audio header:
                [W] offset 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  0123456789abcdef
                [D] offset 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  0123456789abcdef
                [W]      0 00 00 00 00 02 01 00 00                         ........
                [D]      0 02 10 00 00 bb 80 00 00 01 e0 00 00 00 01       ..............
                [I] Audio Header:
                [I]   channels = 2
                [I]   bits = 16
                [I]   rate = 48000
                [I]   frame size = 480
                [I]   unknown = 1
                [I] ChiakiOpusDecoder initialized
                [I] Audio Device alsa_output.pci-0000_04_00.5-platform-acp5x_mach.0.HiFi__hw_acp5x_1__sink opened with 2 channels @ 48000 Hz, buffer size 19200
                [I] Video Profiles:
                [I]   0: 1920x1080
                [I] StreamConnection successfully received streaminfo
                [I] Switched to profile 0, resolution: 1920x1080
                [I] Ctrl received Heartbeat, sending reply
                [I] Ctrl received Heartbeat, sending reply
                [I] StreamConnection is disconnecting
                [I] StreamConnection sending Disconnect
                [I] StreamConnection was requested to stop
                [I] StreamConnection closed takion
                [I] StreamConnection completed successfully
                [I] Ctrl requested to stop
                [I] Ctrl stopped
                [I] Session has quit
                    .__    .__        __   .__   _____     .___             __
                ____ |  |__ |__|____  |  | _|__| /  |  |  __| _/____   ____ |  | __
                _/ ___\|  |  \|  \__  \ |  |/ /  |/   |  |_/ __ |/ __ \_/ ___\|  |/ /
                \  \___|   Y  \  |/ __ \|    <|  /    ^   / /_/ \  ___/\  \___|    <
                \___  >___|  /__(____  /__|_ \__\____   |\____ |\___  >\___  >__|_ \
                    \/     \/        \/     \/       |__|     \/    \/     \/     \/
                                    .__        __             .___._.
                ______ ___________|__|______/  |_  ____   __| _/| |
                /  ___// ___\_  __ \  \____ \   __\/ __ \ / __ | | |
                \___ \  \___|  | \/  |  |_> >  | \  ___// /_/ |  \|
                /____  >\___  >__|  |__|   __/|__|  \___  >____ |  __
                    \/     \/         |__|             \/     \/  \/
                ```

        
    === "Manual Instructions (For People who like to do things by hand)"

        1. Get your console nickname

            1. It was the nickname listed with your console in the console widget. It can also be obtained with

                ```bash
                flatpak run re.chiaki.Chiaki4deck list
                ```

                ???+ example "Example Output"

                    ``` bash
                    Host: PS5-012
                    ```

                    In this example, the console nickname is `PS5-012`, please use your nickname. If you have multiple, choose the one from the list you want and choose the corresponding regist_key in the next step (i.e. if you pick the console on the 2nd line here, pick the regist_key on the 2nd line in the next step).

        2. Get your remote play registration key

            1. Run the following command in your console

                ``` bash
                cat ~/.var/app/re.chiaki.Chiaki4deck/config/Chiaki/Chiaki.conf | grep regist_key | cut -d '(' -f2 | cut -d '\' -f1
                ```

                ???+ example "Example Output"

                    ``` bash
                    2ebf539d
                    ```

                !!! Note "Chiaki Configuration File"

                    This command is printing the Chiaki configuration file generated when you ran `chiaki4deck` for the first time. This is where the flatpak version of Chiaki saves your settings and details. In fact, all flatpaks store their details with the ~/.var/app/app_id pattern. If you want to look at the file itself and get the value you can open the file at `/home/deck/.var/app/re.chiaki.Chiaki4deck/config/Chiaki/Chiaki.conf` and look for the remote play registration key [`rp_regist_key`].


        3. Choose your [default launch option](../updates/done.md#3-view-modes-for-non-standard-screen-sizes){target="_blank" rel="noopener noreferrer"} (fullscreen, zoom, or stretch)

            - fullscreen: A full screen picture with black bars, aspect ratio maintained

            - zoom: A full screen zoomed picture with no black bars, aspect ratio maintained, edges of image cut off by zoom

            - stretch: A full screen stretched picture with no black bars, aspect ratio slightly distorted by vertical [in the case of Steam Deck] stretch

            !!! Info "Default Launch Option"

                This is the option remote play will launch with. You can always toggle to a different option by using the ++ctrl+s++ (toggles between stretch and normal) and ++ctrl+z++ (toggles between zoom and normal) options which should be set if desired in the Steam controller configuration for the game. See [Controller Configuration Section](controlling.md){target="_blank" rel="noopener noreferrer"} for more details.

        4. Create a new blank file located in the folder `/home/deck/.var/app/re.chiaki.Chiaki4deck/config/Chiaki` named  `Chiaki-launcher.sh`

            ```bash
            touch "${HOME}/.var/app/re.chiaki.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh"
            ```
        
        5. Open the `/home/deck/.var/app/re.chiaki.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh` file in your favorite editor and save the following output.

            ``` bash
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
            flatpak run re.chiaki.Chiaki4deck wakeup -<playstation_console> -h <console_ip> -r '<remote_play_registration_key>'
            sleep 1
            # wait for PlayStation to return one successful packet, exit script on error if it never happens
            ping -c 1 -w 20 <console_ip> &>/dev/null
            sleep 1

            # Chiaki handles its own errors, so unsetting trap here.
            trap - ERR

            # Begin playing PlayStation remote play via Chiaki on your Steam Deck :)
            flatpak run re.chiaki.Chiaki4deck --<launch_option> stream '<console_nickname>' <console_ip>
            ```
        
        6. Edit the `/home/deck/.var/app/re.chiaki.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh` file, replacing the following values for your values:

            1. `<console_ip>`

            2. `<console_nickname>`

            3. `<remote_play_registration_key>`

            4. `<launch_option>`

            5. `<playstation_console>` (either 4 or 5)

            ???+ example "Example Script with "fake" values [yours will be different]"

                ``` bash
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
                flatpak run re.chiaki.Chiaki4deck wakeup -5 -h 192.168.1.16 -r '2ebf539d'
                sleep 1
                # wait for PlayStation to return one successful packet, exit script on error if it never happens
                ping -c 1 -w 20 192.168.1.16 &>/dev/null
                sleep 1

                # Chiaki handles its own errors, so unsetting trap here.
                trap - ERR

                # Begin playing PlayStation remote play via Chiaki on your Steam Deck :)
                flatpak run re.chiaki.Chiaki4deck --zoom stream 'PS5-012' 192.168.1.16
                ```

        7. Make the script executable:

            ```bash
            chmod +x "${HOME}/.var/app/re.chiaki.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh"
            ```

4. Test your newly created script (if you haven't already) by running:
        
    ```bash
    "${HOME}/.var/app/re.chiaki.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh"
    ```

    === "Your script worked!"
    
        !!! success "We have liftoff! :rocket:"

            Chiaki launched in your desired screen mode on your Steam Deck! Congratulations!

    === "Your script didn't work..."

        !!! Error "You are no longer on the happy path :weary:"

            === "Script fails before launching stream window"

                Check the error and fix issues if obvious. Otherwise, things to check:
                
                1. Make sure your PlayStation console is in either rest/sleep mode or on (otherwise this won't work)
                
                2. Double check the values you entered in the script to make sure they are correct.
                
                3. Open your script at: `/home/deck/.var/app/re.chiaki.Chiaki4deck/config/Chiaki` and try to run each command individually to see what's causing your problem.
                
                4. If issues persist with this and `chiaki4deck` launches fine regularly via the application link, just not the automation, feel free to reach out. If you think the documentation itself needs to be updated click the :material-heart-broken: underneath "Was this page helpful?" and open the feedback form for this page. If you just need help, you can reach me via [Reddit](https://www.reddit.com/message/compose/?to=Street_Pea_6693){target="_blank" rel="noopener noreferrer"} or [email](mailto:streetpea@proton.me)

            === "Error in stream window"
 
                Make sure your PlayStation console is in either rest/sleep mode or on (otherwise this won't work). If issues persist, feel free to reach out via [Reddit](https://www.reddit.com/message/compose/?to=Street_Pea_6693){target="_blank" rel="noopener noreferrer"} or [email](mailto:streetpea@proton.me). If you think the documentation itself needs to be updated click the :material-heart-broken: underneath "Was this page helpful?" and open the feedback form for this page.