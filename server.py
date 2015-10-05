import socket
import select
import sys
import random
import time

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
print 'Attempting %d runs' % (NUM_RUNS)

commands = ['ps', 'ls', 'cat a.cpp', 'time']
#commands = ['ps', 'time']

numCommands = 1

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

                numCommands -= numReady
                if numCommands != 0 or state == 'done':
                    continue

                s.sendto('exec sosh &\n', sabreAddr)
                s.sendto('exec sosh &\n', sabreAddr)

                numCommands = random.randint(1, 5)

                for i in range(numCommands):
                    command = commands[random.randint(0, len(commands)-1)]
                    command += '\n'
                    s.sendto(command, sabreAddr)

                numCommands += 4

                s.sendto('exit\n', sabreAddr)
                s.sendto('exit\n', sabreAddr)

                count += numReady
                if numReady:
                    print '<<Run %d starting>>' % (count)

                if count >= NUM_RUNS:
                    print 'Finished ' + str(count) + ' runs successfully!'
                    state = 'done'

                

#                #time.sleep(random.uniform(0.01, 0.03))
#                for i in range(numReady):
#                    if state == 'exec':
#                        print 'exec sosh'
#                        s.sendto('exec sosh\n', sabreAddr)
#                        state = 'exit'
#                    elif state == 'exit':
#                        print 'exit'
#                        s.sendto('exit\n', sabreAddr)
#                        state = 'exec'
#                    elif state == 'done':
#                        pass
#                    else:
#                        print 'warning warning'
#                
#
#                count += numReady
#                if numReady:
#                    print '<<Run %d starting>>' % (count)
#
#                if state != 'done' and count >= NUM_RUNS:
#                    print 'Finished ' + str(count) + ' runs successfully!'
#                    state = 'done'
            else:
                time.sleep(0.01)
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
