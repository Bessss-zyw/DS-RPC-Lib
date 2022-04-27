# clean last build files
make clean

# compile demo_server && demo_client
make all

# run demo_server on port 19370
./build/demo_server 19370
# gdb ./demo_server

# next thing to do is open another terminal 
# and run demo_client on the same port
# ./build/demo_client 19370