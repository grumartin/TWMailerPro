#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <string>

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024

using namespace std;

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{

   if(argc < 2){
       cerr << "Missing arguments!" << endl;
       return EXIT_FAILURE;
   }

   int port = atoi(argv[2]);
   int create_socket;
   char buffer[BUF];
   struct sockaddr_in address;
   int size;
   bool isQuit = false;

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as server)
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      perror("Socket error");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // INIT ADDRESS
   // Attention: network byte order => big endian
   memset(&address, 0, sizeof(address)); // init storage with 0
   address.sin_family = AF_INET;         // IPv4
   // https://man7.org/linux/man-pages/man3/htons.3.html
   address.sin_port = htons(port);
   // https://man7.org/linux/man-pages/man3/inet_aton.3.html

   inet_aton(argv[1], &address.sin_addr);

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A CONNECTION
   // https://man7.org/linux/man-pages/man2/connect.2.html
   if (connect(create_socket,
               (struct sockaddr *)&address,
               sizeof(address)) == -1)
   {
      // https://man7.org/linux/man-pages/man3/perror.3.html
      perror("Connect error - no server available");
      return EXIT_FAILURE;
   }

   // ignore return value of printf
   printf("Connection with server (%s) established\n",
          inet_ntoa(address.sin_addr));

   ////////////////////////////////////////////////////////////////////////////
   // RECEIVE DATA
   // https://man7.org/linux/man-pages/man2/recv.2.html
   size = recv(create_socket, buffer, BUF - 1, 0);
   if (size == -1)
   {
      perror("recv error");
   }
   else if (size == 0)
   {
      printf("Server closed remote socket\n"); // ignore error
   }
   else
   {
      buffer[size] = '\0';
      printf("%s", buffer); // ignore error
   }

   do
   {
      printf(">> ");
      string input = "";
      string line = "";

      getline(cin, line);  //get command
      
      
      if("QUIT" == line){
         isQuit = true;
         input += line;
         break;
      }

      else if("LOGIN" == line){
         line += "\n";
         input += line;
         printf(">> ");
         getline(cin, line);
         line += "\n";
         input += line;
         printf(">> ");
         getline(cin, line);
         line += "\n";
         input += line;
      }

      else if("SEND" == line){
         line += "\n";
         input += line;
         while(line!=".\n"){
            printf(">> ");
            getline(cin, line);
            line += "\n";
            input += line;
         }
         //system("clear");
      }

      else if("LIST" == line){
         line += "\n";
         input += line;
      }

      else if("READ" == line || "DEL" == line){
         line += "\n";
         input += line;
         printf(">> ");
         getline(cin, line);
         line += "\n";
         input += line;
      }

      //convert string to char*
      const char* inp_str_const = input.c_str();
      char* inp_str = new char[sizeof(inp_str_const) + 100];
      strcpy(inp_str, inp_str_const);
      
      int size = strlen(inp_str);


/*
      if (inp_str[size - 2] == '\r' && inp_str[size - 1] == '\n')
      {
         size -= 2;
         inp_str[size] = 0;
      }
      else if (inp_str[size - 1] == '\n')
      {
         --size;
         inp_str[size] = 0;
      }
*/

      //////////////////////////////////////////////////////////////////////
      // SEND DATA
      // https://man7.org/linux/man-pages/man2/send.2.html
      // send will fail if connection is closed, but does not set
      // the error of send, but still the count of bytes sent
      if ((send(create_socket, inp_str, size, 0)) == -1) 
      {
         // in case the server is gone offline we will still not enter
         // this part of code: see docs: https://linux.die.net/man/3/send
         // >> Successful completion of a call to send() does not guarantee 
         // >> delivery of the message. A return value of -1 indicates only 
         // >> locally-detected errors.
         // ... but
         // to check the connection before send is sense-less because
         // after checking the communication can fail (so we would need
         // to have 1 atomic operation to check...)
         perror("send error");
         break;
      }


      if(isQuit == true){
         break;
      }

      //////////////////////////////////////////////////////////////////////
      // RECEIVE FEEDBACK
      // consider: reconnect handling might be appropriate in somes cases
      //           How can we determine that the command sent was received 
      //           or not? 
      //           - Resend, might change state too often. 
      //           - Else a command might have been lost.
      //
      // solution 1: adding meta-data (unique command id) and check on the
      //             server if already processed.
      // solution 2: add an infrastructure component for messaging (broker)
      //
      size = recv(create_socket, buffer, BUF - 1, 0);
      if (size == -1)
      {
         perror("recv error");
         break;
      }
      else if (size == 0)
      {
         printf("Server closed remote socket\n"); // ignore error
         break;
      }
      else
      {
         buffer[size] = '\0';
         printf("<< %s\n", buffer); // ignore error
      }

   } while (isQuit != true);

   ////////////////////////////////////////////////////////////////////////////
   // CLOSES THE DESCRIPTOR
   if (create_socket != -1)
   {
      if (shutdown(create_socket, SHUT_RDWR) == -1)
      {
         // invalid in case the server is gone already
         perror("shutdown create_socket"); 
      }
      if (close(create_socket) == -1)
      {
         perror("close create_socket");
      }
      create_socket = -1;
   }

   return EXIT_SUCCESS;
}
