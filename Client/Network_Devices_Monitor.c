#include <unistd.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/wait.h>
#include "../common_definitions.h"
//gcc -o NDM-bin Network_Devices_Monitor.c -lm `pkg-config --cflags --libs gtk+-3.0` -export-dynamic


GtkBuilder *builder;
GtkWidget *window;
GtkWidget *myButton;

const char* server_address;

int main (int argc, char **argv)
{

  if(argc < 2) {
    printf("Usage: %s ip \n", argv[0]);
    return 0;
  }

  server_address = argv[1];

  gtk_init(&argc, &argv); //init Gtk
  
  builder = gtk_builder_new_from_file("Network_Devices_Monitor.glade");
  
  window = GTK_WIDGET(gtk_builder_get_object(builder, "myWindow"));
  myButton = GTK_WIDGET(gtk_builder_get_object(builder, "Online_agent_button"));
  
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
  
  gtk_builder_connect_signals(builder, NULL);
  

  
  /*GtkApplication *app;
  int status;

  app = gtk_application_new ("org.gtk.example", G_APPLICATION_FLAGS_NONE);
  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);*/
  
  gtk_widget_show(window);
  
  gtk_main();

  return 0;
}
//use const gchar* instead of const char*

int get_request(const char* request, char response[MSG_MAX_SIZE]) {
unsigned int retry_counter = 0;
Retry_get_request:
	retry_counter+=1;
	if(retry_counter >= 5) {
		return 0;
	}
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if ( -1 == sockfd ) {
        perror("socket()");
        goto Retry_get_request;
    }
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in server_sockaddr;
    bzero(&server_sockaddr, sizeof(server_sockaddr));
    

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(server_address);
    server_sockaddr.sin_port = htons (CLIENT_PORT);


    int recieved_response = 0;
    bzero(response, MSG_MAX_SIZE);
    
    while(!recieved_response) {
        if (sendto(sockfd, request, strlen(request), 0, (struct sockaddr*)&server_sockaddr, sizeof(server_sockaddr)) < 0) {
            perror("sendto");
            close(sockfd);
            goto Retry_get_request;
        }
        printf("Sent :%s:\n",request);
        if(recv(sockfd, response, MSG_MAX_SIZE, 0) < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
               sleep(1);
            else {
                perror("recv()");
                close(sockfd);
                goto Retry_get_request;
            }
        }
        else {
            recieved_response = 1;
        }
    }
    
    close(sockfd);
    return 1;
}

void on_button_clicked(GtkButton *b, gpointer user_data) {
  g_print ("Hello World\n");
}
static int count = 0;
void on_aglist_refresh_button_clicked(GtkButton *b, gpointer user_data) {
    g_print("Counter: %d\n",count++);
	char response[MSG_MAX_SIZE];
	char request[2];
	request[0] = CLMSG_AGLIST;
	request[1] = '\0';
	get_request(request, response);

	g_print("%s\n",response);
}

