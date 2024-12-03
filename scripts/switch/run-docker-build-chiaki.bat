set my_dir=%cd%
docker run --rm ^
        -v "%my_dir%:/build/chiaki":z ^
        -w "/build/chiaki" ^
        docker.io/xlanor/chiaki-ng-switch-builder:latest ^
        /bin/bash -c "scripts/switch/build.sh"

if %ErrorLevel% equ 0 (copy build_switch\switch\chiaki.nro build_switch\switch\chiaki-ng.nro)
