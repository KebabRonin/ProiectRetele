#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
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
#include "../../common_definitions.h"

static const char* server_address;
//gcc -o NDM Network_Devices_Monitor.c -lm `pkg-config --cflags --libs gtk+-3.0` -export-dynamic


GtkBuilder *builder;
GtkWidget *window;
GtkWidget *refresh_button;
GtkWidget *spinner;
GtkWidget *agent_button_list;
GtkWidget *main_dashboard;
GtkTreeModel *online_ag_liststore;
GtkTreeModel *offline_ag_liststore;
GtkWidget *agent_toadd_combobox;
//GtkWidget *agnt_dashboard;
struct text_int_param{
    const gchar* text;
    int integer;
} param[100];
int nr_param = 0;

static const char* server_address = NULL;

void wid_agent_list();

void gtk_container_destroyall(GtkContainer* container) {
    void widget_destroy(GtkWidget* my_widget, gpointer data) {
        gtk_widget_destroy(my_widget);
    }
    gtk_container_forall(GTK_CONTAINER(container), widget_destroy, 0);
}

int main (int argc, char **argv)
{

    if(argc < 2) {
        g_print("Usage: %s ip \n", argv[0]);
        return 0;
    }

    server_address = argv[1];

    gtk_init(&argc, &argv); //init Gtk

    builder = gtk_builder_new_from_file("Network_Devices_Monitor_nou.glade");

    window = GTK_WIDGET(gtk_builder_get_object(builder, "myWindow"));

    main_dashboard = GTK_WIDGET(gtk_builder_get_object(builder, "main_dashboard"));

    agent_toadd_combobox = GTK_WIDGET(gtk_builder_get_object(builder, "agent_toadd_combobox"));

    online_ag_liststore = GTK_TREE_MODEL(gtk_builder_get_object(builder, "online_ag_liststore"));
    offline_ag_liststore = GTK_TREE_MODEL(gtk_builder_get_object(builder, "offline_ag_liststore"));
    
    spinner = GTK_WIDGET(gtk_spinner_new());

    gtk_spinner_start(GTK_SPINNER(spinner));

    
    refresh_button = GTK_WIDGET(gtk_builder_get_object(builder, "refresh_button"));
    agent_button_list = GTK_WIDGET(gtk_builder_get_object(builder, "ag_button_list"));
    //agent_list = GTK_WIDGET(gtk_builder_get_object(builder, "agents_list"));

    g_signal_connect (window, "destroy", G_CALLBACK (gtk_main_quit), NULL);

    gtk_builder_connect_signals(builder, NULL);

    //style
/*
    GtkCssProvider *provider;
    provider = gtk_css_provider_new();
    //Connect screen with provider.
    GdkDisplay *display = gdk_display_get_default ();
    GdkScreen *screen = gdk_display_get_default_screen (display);

    gtk_style_context_add_provider_for_screen (GDK_SCREEN(window), GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    //Give button color.
    gtk_css_provider_load_from_data(GTK_CSS_PROVIDER(provider), 
    "#testBtnOnline {background:blue; color:white}\n#testBtnOffline {background:red ; color:white}", -1 , NULL);
*/
    wid_agent_list();
    gtk_widget_show(window);

    gtk_main();

    return 0;
}
//use const gchar* instead of const char*

static int count = 0;

GtkWidget* build_agprop_wid(const gchar* name) {
    GtkWidget* self = gtk_label_new(name);
    return self;
}

void show_ag_dashboard(GtkWidget* mybutton, struct text_int_param* param) {
    const gchar* name = param->text;
    int online = param->integer;
    g_print("%s %d\n",name, online);

    if(online == 1) {
        gtk_combo_box_set_model ( GTK_COMBO_BOX(agent_toadd_combobox), GTK_TREE_MODEL(online_ag_liststore));
    }
    else {
        gtk_combo_box_set_model ( GTK_COMBO_BOX(agent_toadd_combobox), GTK_TREE_MODEL(offline_ag_liststore));
    }
    

    //gtk_container_destroyall(GTK_CONTAINER(main_dashboard));



    GtkWidget* prop_wid = build_agprop_wid(name);
    gtk_box_pack_start (GTK_BOX (main_dashboard), prop_wid, 0, 1, 5);

    gtk_widget_show_all(main_dashboard);
}

void dummy_func() {
    g_print("Hello World!\n");
}

int get_request(char* request, char response[MSG_MAX_SIZE]) {
    unsigned int retry_counter = 0;
    int len;
    #ifdef cl_debug
g_print(COLOR_CL_DEB); 
    g_print("Sending :%s:",request); fflush(stdin);
    g_print(COLOR_OFF);
fflush(stdout);
#endif
    buffer_change_endian(request, strlen(request));

Retry_get_request:
	retry_counter+=1;
	if(retry_counter >= 5) {
        g_print("\n");
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


    int recieved_response = 0;
    bzero(response, MSG_MAX_SIZE);
    
    while(!recieved_response) {
        if (sendto(sockfd, request, strlen(request), MSG_NOSIGNAL, (struct sockaddr*)&server_sockaddr, sizeof(server_sockaddr)) < 0) {
            perror("sendto");
            close(sockfd);
            goto Retry_get_request;
        }
        g_print("."); fflush(stdout);
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
            recieved_response = 1;
        }
    }
    g_print("\n");
    close(sockfd);
    return 1;
}

void add_agent_button(char* name, int online) {
    GtkWidget* button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);

    GtkWidget* label = gtk_label_new((const gchar*) name);
    gtk_label_set_xalign (GTK_LABEL(label), 0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);

    char format[60];
    char *markup;
    GtkTreeIter* iter;
    if (online == 1) {
        strcpy(format,"<span foreground=\"#FFFFFF\" style=\"bold\">\%s</span>");
        //gtk_list_store_append(GTK_LIST_STORE(online_ag_liststore), iter);
        //gtk_list_store_set(GTK_LIST_STORE(online_ag_liststore), iter, 0, name, -1);
    }
    else {
        strcpy(format,"<span foreground=\"#888A85\" style=\"italic\">\%s</span>");

        //gtk_list_store_append(GTK_LIST_STORE(offline_ag_liststore), iter);

        //gtk_list_store_set(GTK_LIST_STORE(offline_ag_liststore), iter, 0, name, -1);

    }
    markup = g_markup_printf_escaped (format, name);
    gtk_label_set_markup (GTK_LABEL (label), markup);
    g_free (markup);



    gtk_container_add (GTK_CONTAINER (button), label);
    gtk_container_add (GTK_CONTAINER (agent_button_list), button);

    param[nr_param].text = gtk_label_get_text(GTK_LABEL(label));
    param[nr_param].integer = online;
    nr_param++;
    //g_signal_connect (button, "clicked", G_CALLBACK (show_ag_dashboard), &(param[nr_param]));


    gtk_widget_show_all(button);
}

void wid_agent_list() {
    nr_param = 0;
    gtk_container_destroyall(GTK_CONTAINER(agent_button_list));
    //gtk_container_add(GTK_CONTAINER(agent_button_list), GTK_WIDGET(spinner));
    
    gtk_list_store_clear(GTK_LIST_STORE(online_ag_liststore));
    gtk_list_store_clear(GTK_LIST_STORE(offline_ag_liststore));

    char response[MSG_MAX_SIZE];
    char request[2];
    request[0] = CLMSG_AGLIST;
    request[1] = '\0';
    get_request(request, response);

    

    char* p = strtok(response, "\n");

    while( p != NULL) {
        if (0 == strcmp(p + strlen(p) - 3, "(*)")) {
            p[strlen(p) - 3] = '\0';
            g_print("online: %s\n", p);
            add_agent_button(p, 1);
            p[strlen(p) - 3] = '(';
        }
        else {
            g_print("%s\n", p);
            add_agent_button(p, 0);
        }
        
        p = strtok(NULL, "\n");
    }

    
}

void wid_agent_properties(char* id) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AGPROP;
    sprintf(request+1, "%s", id);
    get_request(request, response);

    
    g_print("%s\n",response);
    
}

void wid_agent_add_is(char* id, char* path) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_ADDSRC;
    sprintf(request+1, "%s\n%s", id, path);
    get_request(request, response);

    
    g_print("%s\n",response);
    
}

void wid_agent_add_rule(char* id, char* path, char* rule_name, char* rule) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_ADDRLE;
    if(MSG_MAX_SIZE < sprintf(request+1, "%s\n%s\n%s|%s", id, path, rule_name, rule))
        g_print("WARNING: message too long!\n");
    else
        get_request(request, response);

    
    g_print("%s\n",response);
    
}

void wid_agent_c_query(char* id, char* path, char* conditions) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_COUNT_QUERY;
    if(MSG_MAX_SIZE < sprintf(request+1, "%s\n%s\n%s", id, path, conditions))
        g_print("WARNING: message too long!\n");
    else
        get_request(request, response);

    
    g_print("There were %s entries matching the conditions.\n",response);
    
}

void wid_agent_rm_rule(char* id, char* path, char* rule_name) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_RMVRLE;
    sprintf(request+1, "%s\n%s\n%s", id, path, rule_name);
    get_request(request, response);

    
    g_print("%s\n",response);
    
}

void wid_agent_howmany(char* id, char* path) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AG_HOWMANY_RULEPAGES;
    sprintf(request+1, "%s\n%s", id, path);
    get_request(request, response);

    if (atoi(response) != 0) {
        g_print("Agent has %d rule pages\n", atoi(response));
    }
    else {
        g_print("%s\n", response);
    }
    
}

void wid_agent_rulenames(char* id, char* path, char* page_nr) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AG_LIST_RULEPAGE;
    sprintf(request+1, "%s\n%s\n%s", id, path, page_nr);
    get_request(request, response);

    
    g_print("Rules on page %s: %s\n", page_nr, response);
    
}

void wid_agent_showrule(char* id, char* path, char* rule_name) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AG_SHOW_RULE;
    sprintf(request+1, "%s\n%s\n%s", id, path, rule_name);
    get_request(request, response);

    
    g_print("Rule %s: %s\n", rule_name, response);
    
}

void wid_agent_lsinfo(char* id) {
    char response[MSG_MAX_SIZE];
    char request[MSG_MAX_SIZE];
    request[0] = CLMSG_AG_LIST_SOURCES;
    sprintf(request+1, "%s", id);
    get_request(request, response);

    
    g_print("%s\n", response);
    
}

void help() {
    #define MYCOLOR  "\033[1;33m"
    #define NOTDONE  "\033[1;31m"

    g_print(MYCOLOR "======================" COLOR_OFF "\n");
    g_print(MYCOLOR "exit" COLOR_OFF " - \n");
    g_print(MYCOLOR "help" COLOR_OFF " - show this\n");
    g_print(MYCOLOR "list" COLOR_OFF " - list all agents\n");
    g_print(MYCOLOR "run <file-name>" COLOR_OFF " - run <file-name> as script in client terminal\n");
    g_print(MYCOLOR "prop <agent-name>" COLOR_OFF " - show info on <agent-name>\n");
    g_print(MYCOLOR "lsinfo <agent-name>" COLOR_OFF " - show active info sources of <>agent-name>\n");
    g_print(MYCOLOR "howmany <agent-name> <path>" COLOR_OFF " - show number of rule pages (there are %d rules/page)\n", ENTRIESPERPAGE);
    g_print(MYCOLOR "add-source <agent-name> <path>" COLOR_OFF " - add file from <path> to the info sources of <agent-name>\n");
    g_print(MYCOLOR "rulenames <agent-name> <path> <page>" COLOR_OFF " - show names of all rules on page <page>\n");
    g_print(MYCOLOR "rm-rule <agent-name> <path> <rule-name>" COLOR_OFF " - remove rule (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
    g_print(MYCOLOR "c-query <agent-name> <path> `<conditions>" COLOR_OFF " - ask for entries matching <conditions> from the log of <path> in <agent-name>\n");
    g_print(NOTDONE "<conditions>" COLOR_OFF " - \'&\' separated conditions of type :<name><op>\"<value>\":\nWhere <op> can be one of {=!<>}\nNO SPACES OUTSIDE \"\"!!\n");
    g_print(MYCOLOR "showrule <agent-name> <path> <rule-name>" COLOR_OFF " - show actual rule refered to as <rule-name>\n");
    g_print(MYCOLOR "add-rule <agent-name> <path> <rule-name> `<rule>" COLOR_OFF " - add <rule> (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
    g_print(MYCOLOR "======================" COLOR_OFF "\n");
}

int legacy_main(int argc, char* argv[]) {
    if(argc < 2) {
        g_print("Usage: %s ip \n", argv[0]);
        return 0;
    }
    
    server_address = argv[1];

    help();

    char buffer[300]; bzero(buffer, 300);
    int already_read = 0, read_now;

    char* p;

    int stdin_backup = dup(0);

    int done_reading_file = 0;

    while(1) {
        already_read = strlen(buffer);
        if(0 >= (read_now = read(0, buffer + already_read, 300 - already_read))) {
            if(read_now == 0) {
                if(done_reading_file == 0) {
                    done_reading_file = 1;
                    strcat(buffer,"\n");
                    already_read += 1;
                }
                if (already_read == 0) {
                    dup2(stdin_backup, 0);
                    bzero(buffer, 300);
                    g_print("Done executing script\n");
                    g_print("===============\n");
                    continue;
                } 
            }
            else {
                perror("read");
                return 1;
            }
            
        }
        already_read += read_now;
        buffer[already_read] = '\0';

        p = strstr(buffer, "\\\n");

        while(p != NULL) {
            p[0] = ' ';
            for(int i = 1; i <= strlen(p); ++i) {
                p[i] = p[i+1];
            }
            p = strstr(buffer, "\\\n");
        }

        if (NULL != (p = strchr(buffer, '\n'))) {
            p[0] = '\0';
            p += 1;
            char* prev = buffer, *tok = buffer;
            char* args[10];
            int nr_args = 0;

            while( tok != NULL) {

                tok = strchr(tok, ' ');
                if(tok != NULL) {
                    if(tok[1] == '`') {
                        tok[0] = '\0';
                        args[nr_args++] = prev;
                        prev = tok + 2;
                        args[nr_args++] = prev;
                        break;
                    }
                    else {
                        tok[0] = '\0';
                        tok += 1;
                    }
                }

                args[nr_args++] = prev;
                prev = tok;
            }
            if(nr_args < 1) {
                continue;
            }
            if(strcmp(args[0], "help") == 0) {
                help();
            }
            else if(strcmp(args[0], "exit") == 0) {
                return 0;
            }
            else if(strcmp(args[0], "list") == 0) {
                g_print("===============\n");
                wid_agent_list();
                g_print("===============\n");
            }
            else if(strcmp(args[0], "prop") == 0) {
                if(nr_args < 2) {
                    g_print("\nUsage: prop <agent-name> - show info on <agent-name>\n");
                }
                else {
                    g_print("===============\n");
                    wid_agent_properties(args[1]);
                    g_print("===============\n");
                }
            }
            else if(strcmp(args[0], "add-source") == 0) {
                if(nr_args < 3) {
                    g_print("\nUsage: add-source <agent-name> <path> - add file from <path> to the info sources of <agent-name>\n");
                }
                else {
                    g_print("===============\n");
                    wid_agent_add_is(args[1], args[2]);
                    g_print("===============\n");
                }
            }
            else if(strcmp(args[0], "add-rule") == 0) {
                if(nr_args < 5) {
                    g_print("\nUsage: add-rule <agent-name> <path> <rule-name> `<rule> - add <rule> (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
                }
                else {
                    g_print("===============\n");
                    wid_agent_add_rule(args[1], args[2], args[3], args[4]);
                    g_print("===============\n");
                }
            }
            else if(strcmp(args[0], "rm-rule") == 0) {
                if(nr_args < 4) {
                    g_print("\nUsage: rm-rule <agent-name> <path> <rule-name> - remove rule (referred to as <rule-name>) to watch in file from <path> of <agent-name>\n");
                }
                else {
                    g_print("===============\n");
                    wid_agent_rm_rule(args[1], args[2], args[3]);
                    g_print("===============\n");
                }
            }
            else if(strcmp(args[0], "run") == 0) {
                if(nr_args < 2) {
                    g_print("\nUsage: run <file-name> - run <file-name> as script in client terminal\n");
                }
                else {
                    int fd = open(args[1], O_RDONLY, 0);
                    if(fd < 0) {
                        perror("open");
                    }
                    else {
                        done_reading_file = 0;
                        g_print("===============\n");
                        g_print("Running script..\n");
                        dup2(fd, 0);
                        close(fd);
                        bzero(buffer, 300);
                    }
                }
            }
            else if(strcmp(args[0], "howmany") == 0) {
                if(nr_args < 3) {
                    g_print("\nUsage: howmany <agent-name> <path> - show number of rule pages (there are %d rules/page)\n", ENTRIESPERPAGE);
                }
                else {
                    g_print("===============\n");
                    wid_agent_howmany(args[1], args[2]);
                    g_print("===============\n");
                }
            }
            else if(strcmp(args[0], "rulenames") == 0) {
                if(nr_args < 4) {
                    g_print("\nUsage: rulenames <agent-name> <path> <page> - show names of all rules on page <page>\n");
                }
                else {
                    g_print("===============\n");
                    wid_agent_rulenames(args[1], args[2], args[3]);
                    g_print("===============\n");
                }
            }
            else if(strcmp(args[0], "showrule") == 0) {
                if(nr_args < 4) {
                    g_print("\nUsage: showrule <agent-name> <path> <rule-name> - show actual rule refered to as <rule-name>\n");
                }
                else {
                    g_print("===============\n");
                    wid_agent_showrule(args[1], args[2], args[3]);
                    g_print("===============\n");
                }
            }
            else if(strcmp(args[0], "lsinfo") == 0) {
                if(nr_args < 2) {
                    g_print("\nUsage: lsinfo <agent-name>" COLOR_OFF " - show active info sources of <>agent-name>\n");
                }
                else {
                    g_print("===============\n");
                    wid_agent_lsinfo(args[1]);
                    g_print("===============\n");
                }
            }
            else if(strcmp(args[0], "c-query") == 0) {
                if(nr_args < 4) {
                    g_print("\nUsage: c-query <agent-name> <path> `<conditions> - ask for entries matching <conditions> from the log of <path> in <agent-name>\n");
                }
                else {
                    g_print("===============\n");
                    wid_agent_c_query(args[1], args[2], args[3]);
                    g_print("===============\n");
                }
            }
            else if (strlen(args[0]) > 0) {
                g_print("Unrecognised command: %s\n", args[0]);
                //help();
            }

            for( int i = 0; i <= strlen(p); ++i) {
                buffer[i] = p[i];
            }

            //sprintf(buffer, "%s", p+1);
        }
    }
}
