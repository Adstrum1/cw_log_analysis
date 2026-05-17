#include <gtkmm/application.h>
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    return Gtk::Application::create("org.spz.logcollector")->make_window_and_run<MainWindow>(argc, argv);
}
