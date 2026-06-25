#include <QApplication>
#include <QMainWindow>

#include <dsr/api/dsr_api.h>
#include <dsr/gui/dsr_gui.h>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const std::string graph_file = argc > 1 ? argv[1] : std::string{};

    DSR::GraphSettings settings{
        900,
        4,
        1,
        "delta_churn_live_viewer",
        graph_file,
        "",
        true,
        DSR::GraphSettings::LOGLEVEL::INFOL,
        0,
        DSR::SignalMode::QT,
        DSR::SyncMode::LWW,
    };
    auto graph = std::make_shared<DSR::DSRGraph>(settings);

    QMainWindow window;
    window.resize(900, 700);
    DSR::DSRViewer viewer(&window, graph, DSR::DSRViewer::view::graph, DSR::DSRViewer::view::graph);

    return app.exec();
}
