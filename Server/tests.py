import socket
import time
import os

def setupSocket(ip, port):
	so = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	so.connect((ip, port))
	return so

def runFailTest(sock):
	sock.send("NEW_TXN -100 0 0 13\r\n\r\ntestfile.txt")
	sock.send("oisdlkfnan")
	temp = sock.recv(1024)
	print temp

def runPassTest(sock, testNum):
	sock.send("NEW_TXN -1 0 12\r\n\r\ntestfile.txt")
	received = sock.recv(1024)
	tokenized = received.split()
	txnId = tokenized[1]
	print "txn id: " + txnId		
	payload = "testfile.txt"+str(testNum)
	write =	"WRITE " +txnId + " 1 " + str(len(payload))+"\r\n\r\ntestfile.txt"+str(testNum)
	sock.send(write)	

	received = sock.recv(1024)
	print received	

	write = "COMMIT " + txnId + " 1 0\r\n\r\n\r\n"
	print write	
	sock.send(write)

	received = sock.recv(1024) 
	print received

def bugReplica():
	s = setupSocket("127.0.0.1", 8000)
	
def runRangePassTests(num):
	for i in range(0,num):
		s = setupSocket("127.0.0.1", 8080)
		if s <= -1:
			print "Socket could not be initialized"
			exit()
		runPassTest(s, i)
		s.close()

def main():
	runRangePassTests(30)	

		
main()
