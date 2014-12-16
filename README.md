TCP-IP-Primary-Replica-File-Server
==================================
WARNINGS: 
This was a first-time TCP/IP Socket project with rusty C++ useage. Use at your own risk.
The number of connections allowed is dependent on what you're building on (right now set to 256). 
The recovery agent doesn't work (yet).
    



To run the primary initialization, use this:

EXAMPLE To initiate a primary server that will write out info for replicas and clients to reach it at
./server -i 127.0.0.1 -p 8080 -d ~/Documents/testfs -n ~/Documents/primary.txt -t primary

EXAMPLE To initiate a replica server that checks the primary.txt file the server above uses
./server -i 127.0.0.1 -p 8000 -d ~/Documents/testfsbak -n ~/Documents/primary.txt -t replica

-i : ip address you'd like this server to operate as (exits if unable to bind) - binds to 127.0.0.1 if not included (OPTIONAL)
-p : port you'd like this server to use. DO NOT USE PORT 3000 as this is what primary uses to communicate with its replica - uses 8080 (OPTIONAL)
-d : directory that this filesystem server will operate on. REQUIRED.
-n : ABSOLUTE path of the primary.txt file that the primary and its replicas will see. Writes out X.X.X.X PORT to it  (ip and port)
     that any replica will use to communicate with this primary. If it already exists, it will truncate it and write from fresh. REQUIRED.
-t : type of server. Must be either "primary" or "replica" (excluding the quotation marks). REQUIRED.


When the primary crashes, the  primary.txt file will be updated to the address

IMPORTANT: Do not use port 3000, it is the replica/primary connection port

TO BUILD:
in terminal, run "make" (excluding quotation marks :) this will build the binary 'server'


FOR YOUR CONVINEINCE
There are two files included: primary.sh and replica.sh, and two directories, testfs and testfsbak. If you'd like to use these as the stage to check that files are consistent, just run primary.sh to start the primary server. Then, run replica.sh to start the replica. Everything takes care of it self after that.
