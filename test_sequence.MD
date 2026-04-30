make clean
make
./nexs
exec ("example/minios_dev_readreadme/boot.nx")
:exit
./nexs example/minios_dev_readreadme/boot.nx  
make clean
make baremetal-amd64 
'/Users/olmo/Documents/git/base-nexs/scripts/qemu-amd64.sh'
ls
:exit
./nexs --compile example/minios_dev_readreadme/boot.nx --target baremetal-amd64 -o build/baremetal-amd64/nexs.elf
'/Users/olmo/Documents/git/base-nexs/scripts/qemu-amd64.sh'
:exit
./nexs --compile example/minios_dev_readreadme/boot.nx --target macos-amd64 -o test.out                          
