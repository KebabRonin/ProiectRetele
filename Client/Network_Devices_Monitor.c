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

static const char* server_address = NULL;

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
  //agent_list = GTK_WIDGET(gtk_builder_get_object(builder, "agents_list"));
  
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

void on_button_clicked(GtkButton *b, gpointer user_data) {
  g_print ("Hello World\n");
}
static int count = 0;


int get_request(char* request, char response[MSG_MAX_SIZE]) {
    unsigned int retry_counter = 0;
    int len;
    buffer_change_endian(request, strlen(request));

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
    //fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in server_sockaddr;
    bzero(&server_sockaddr, sizeof(server_sockaddr));
    

    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_addr.s_addr = inet_addr(server_address);
    server_sockaddr.sin_port = htons (CLIENT_PORT);

    bzero(response, MSG_MAX_SIZE);
    
    while(1) {
        if (sendto(sockfd, request, strlen(request), MSG_NOSIGNAL, (struct sockaddr*)&server_sockaddr, sizeof(server_sockaddr)) < 0) {
            perror("sendto");
            close(sockfd);
            goto Retry_get_request;
        }
        printf("Sent :%s:\n",request);
        sleep(1);
        if( 0 > (len = recv(sockfd, response, MSG_MAX_SIZE, MSG_DONTWAIT | MSG_NOSIGNAL))) {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
               ;
            else {
                perror("recv()");
                close(sockfd);
                goto Retry_get_request;
            }
        }
        else {
            buffer_change_endian(response, len);
            break;
        }
    }
    
    close(sockfd);
    return 1;
}

void add_online_agent(const char* name) {
    GtkWidget *button;
    button = gtk_button_new_with_label(name);
    //gtk_buildable_add_child (GTK_BUILDABLE(agents_list), button);
    gtk_widget_show(button);
}

void on_aglist_refresh_button_clicked() {
    if(server_address == NULL) {
        g_print("Not connected\n");
        GtkWidget *popup_conn = gtk_window_new(GTK_WINDOW_POPUP);
        gtk_widget_show_all(popup_conn);
    }
    char response[MSG_MAX_SIZE];
    char request[2];
    request[0] = CLMSG_AGLIST;
    request[1] = '\0';
    get_request(request, response);

    char* p = strtok(response, "\n");

    while( p != NULL) {
        if (0 == strcmp(p + strlen(p) - 3, "(*)")) {
            g_print("online ");
            p[strlen(p) - 3] = '\0';
            add_online_agent(p);
            p[strlen(p) - 3] = '(';
        }
        else {
            //add_offline_agent(p);
        }
        g_print("%s\n",p);
        p = strtok(NULL, "\n");
    }

    
}

void wid_agent_properties(char* id) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AGPROP;
    sprintf(request+1, "%s", id);
    get_request(request, response);

    printf("%s\n",response);
}

void wid_agent_add_is(char* id, char* path) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_ADDSRC;
    sprintf(request+1, "%s\n%s", id, path);
    get_request(request, response);

    printf("%s\n",response);
}
