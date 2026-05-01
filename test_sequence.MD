make clean
make
./nexs
exec ("example/minios/boot.nx")
:exit

./nexs example/minios/boot.nx
:exit

make clean
make baremetal-amd64 
'/Users/olmo/Documents/git/base-nexs/scripts/qemu-amd64.sh'
ls
:exit

make clean
make baremetal-amd64 
'/Users/olmo/Documents/git/base-nexs/scripts/make-iso.sh'
'/Users/olmo/Documents/git/base-nexs/scripts/qemu-amd64.sh' --iso  
:exit

./nexs --compile example/minios/boot.nx --target baremetal-amd64 -o build/baremetal-amd64/nexs.elf
'/Users/olmo/Documents/git/base-nexs/scripts/qemu-amd64.sh'
:exit
'/Users/olmo/Documents/git/base-nexs/scripts/make-iso.sh'
'/Users/olmo/Documents/git/base-nexs/scripts/qemu-amd64.sh' --iso  
:exit

make clean
make baremetal-amd64 
'/Users/olmo/Documents/git/base-nexs/scripts/make-iso.sh'
'/Users/olmo/Documents/git/base-nexs/scripts/qemu-amd64.sh' --iso  
:exit

./nexs --compile example/minios/boot.nx --target macos-amd64 -o test.out   
./test.out
                    
