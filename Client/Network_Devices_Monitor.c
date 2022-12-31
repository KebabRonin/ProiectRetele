#include <unistd.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <math.h>
#include <ctype.h>
//gcc -o NDM-bin Network_Devices_Monitor.c -lm `pkg-config --cflags --libs gtk+-3.0` -export-dynamic
static void print_hello (GtkWidget *widget, gpointer data)
{
  g_print ("Hello World\n");
}
GtkBuilder *builder;
GtkWidget *window;
GtkWidget *myButton;

int main (int argc, char **argv)
{
  gtk_init(&argc, &argv); //init Gtk
  
  builder = gtk_builder_new_from_file("Network_Devices_Monitor.glade");
  
  window = GTK_WIDGET(gtk_builder_get_object(builder, "myWindow"));
  myButton = GTK_WIDGET(gtk_builder_get_object(builder, "button"));
  
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
  //printf("Hello World!");
  g_print ("Hello World\n");
}

