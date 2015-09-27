import socket
import select
import sys

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

port = 26706
s.bind(('0.0.0.0', port)) # Listen on all interfaces
s.setblocking(0)

print("Server now awaiting client connection on port %s" % (port))

input = [s, sys.stdin]

running = True

sabreAddr = False
state = 'exec'
count = 0
NUM_RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 10
print 'Attempting %s runs' % (NUM_RUNS)

while running:
    # when there's something in input, then we move forward
    # ignore what's in output and except because there's nothing
    # when it comes to sockets
    inputready, outputready, exceptready = select.select(input, [], [])

    for x in inputready:
        if x == s:
            data, addr = s.recvfrom(4096)
            if data:
                sys.stdout.write(data)
                sys.stdout.flush()
                sabreAddr = addr

                numReady = data.count('$')

                for i in range(numReady):
                    if state == 'exec':
                        print 'exec sosh'
                        s.sendto('exec sosh\n', sabreAddr)
                        state = 'exit'
                    elif state == 'exit':
                        print 'exit'
                        s.sendto('exit\n', sabreAddr)
                        state = 'exec'
                    elif state == 'done':
                        pass
                    else:
                        print 'warning warning'
                count += numReady

                if state != 'done' and count >= NUM_RUNS:
                    print 'Finished ' + str(count) + ' runs successfully!'
                    state = 'done'
            else:
                sleep(0.01)
        elif x == sys.stdin:
            # handle standard input
            stuff = sys.stdin.readline()
            if '/' in stuff[:1]:
                if 'restart' in stuff:
                    state = 'exec'
                    count = 0
            if sabreAddr:
                s.sendto(stuff, sabreAddr)
            else:
                print 'Sabre not connected'
        else:
            print 'This should not happen'

s.close()
