make clean
make
./nexs
exec ("example/minios_dev_readreadme/boot.nx")
:exit

./nexs example/minios_dev_readreadme/boot.nx
:example

make clean
make baremetal-amd64 
'/Users/olmo/Documents/git/base-nexs/scripts/qemu-amd64.sh'
ls
:exit

make clean
make baremetal-amd64 
'/Users/olmo/Documents/git/base-nexs/scripts/make-iso.sh'
qemu-system-x86_64 -drive file=build/nexs-amd64.iso,format=raw -m 128M -nographic
:exit

./nexs --compile example/minios_dev_readreadme/boot.nx --target baremetal-amd64 -o build/baremetal-amd64/nexs.elf
'/Users/olmo/Documents/git/base-nexs/scripts/qemu-amd64.sh'
:exit

make clean
make baremetal-amd64 
'/Users/olmo/Documents/git/base-nexs/scripts/make-iso.sh'
qemu-system-x86_64 -drive file=build/nexs-amd64.iso,format=raw -m 128M -nographic
:exit

./nexs --compile example/minios_dev_readreadme/boot.nx --target macos-amd64 -o test.out   
./test.out
                    
