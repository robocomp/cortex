#include <Qt3DExtras/qcuboidgeometry.h>
#include <dsr/gui/viewers/qt3d_viewer/qt3d_viewer.h>
#include <qgeometry.h>
#include <qnamespace.h>
#include <qphongalphamaterial.h>
#include <qsceneloader.h>
#include <qt5/Qt3DExtras/qcuboidgeometry.h>
#include <qt5/Qt3DExtras/qphongmaterial.h>
#include <qt5/Qt3DRender/qmaterial.h>

namespace DSR {
    
    QT3DViewer::QT3DViewer(std::shared_ptr<DSR::DSRGraph> g_) : g(std::move(g_)), inner(g->get_inner_eigen_api())
    {
        only_one_widget = false;
        view = new Qt3DExtras::Qt3DWindow;
        widget = create_widget();
        //widget->setMinimumSize(QSize(500, 400));

        connect(g.get(), &DSR::DSRGraph::update_node_signal, this, &QT3DViewer::update_node, Qt::QueuedConnection);
        connect(g.get(), &DSR::DSRGraph::update_edge_signal, this, &QT3DViewer::update_edge, Qt::QueuedConnection);
        connect(g.get(), &DSR::DSRGraph::del_edge_signal, this, &QT3DViewer::delete_edge, Qt::QueuedConnection);
        connect(g.get(), &DSR::DSRGraph::del_node_signal, this, &QT3DViewer::delete_node, Qt::QueuedConnection);
        //connect(g.get(), &DSR::DSRGraph::update_node_attr_signal, this, &QT3DViewer::update_node_attr, Qt::QueuedConnection);
        //connect(g.get(), &DSR::DSRGraph::update_edge_attr_signal, this, &QT3DViewer::update_edge_attr, Qt::QueuedConnection);
        

        initialize();
    }
    
    QT3DViewer::~QT3DViewer()
    {
        if (!only_one_widget) {
            delete view;
        }
    }

    QWidget *QT3DViewer::create_widget()
    {
        if (!only_one_widget) {
            only_one_widget = true;
            return QWidget::createWindowContainer(view);
        }
        return nullptr;
    }
    
    QWidget *QT3DViewer::get_widget()
    {
        return widget;
    }

    void QT3DViewer::show()
    {
        view->show();
    }

    void QT3DViewer::initialize()
    {
        rootEntity = new Qt3DCore::QEntity;

        assert(widget);
        widget->setMinimumSize(QSize(500, 500));
        widget->setMaximumSize(QSize(1000, 900));

        //Ligth
        auto *lightEntity = new Qt3DCore::QEntity(rootEntity);
        auto *light = new Qt3DRender::QPointLight(lightEntity);
        light->setColor(QColor(qRgba(250, 190, 160, 180)));
        light->setIntensity(.4);
        lightEntity->addComponent(light);
        auto *lightTransform = new Qt3DCore::QTransform(lightEntity);
        lightTransform->setTranslation(QVector3D(0., 0., 4000.));
        lightEntity->addComponent(lightTransform);

        //Camera
        Qt3DRender::QCamera *camera = view->camera();
        camera->lens()->setPerspectiveProjection(55.0f, 16.0f/9.0f, 0.01f, 10000.0);
        camera->setPosition(QVector3D(170.386, 334.344, 620.43));
        camera->setViewCenter(QVector3D(0, 0, 0));

        Qt3DExtras::QOrbitCameraController *cc = new Qt3DExtras::QOrbitCameraController(rootEntity);
        cc->setLinearSpeed(60.0);
        cc->setLookSpeed(200.0);
        cc->setCamera(camera);
       
        // Framegraph root node
        auto surfaceSelector = new Qt3DRender::QRenderSurfaceSelector();
        auto mainViewPort = new Qt3DRender::QViewport(surfaceSelector);

        //clear buffers
        auto clearBuffers = new Qt3DRender::QClearBuffers(mainViewPort);
        clearBuffers->setBuffers(Qt3DRender::QClearBuffers::ColorDepthBuffer);
        clearBuffers->setClearColor(Qt::white);
        [[maybe_unused]] auto noDraw = new Qt3DRender::QNoDraw(clearBuffers);

        // viewport
        auto viewPort1 = new Qt3DRender::QViewport(mainViewPort);
        viewPort1->setNormalizedRect(QRectF(0.0f, 0.0f, 1.0f, 1.0f));
        auto cameraSelector1 = new Qt3DRender::QCameraSelector(viewPort1);
        cameraSelector1->setCamera(camera);

        view->setActiveFrameGraph(surfaceSelector);

        globalLayer = new Qt3DRender::QLayer(rootEntity);
        globalLayer->setRecursive(false);
        rootEntity->addComponent(globalLayer);

        //TODO: More types?
        for (const auto& node : g->get_nodes_by_types({"plane", "mesh"}))
        {
            update_qt3d_entity(node);
        }
        
        view->setRootEntity(rootEntity);
    }

    void QT3DViewer::update_qt3d_entity(const Node& node)
    {

        if (auto it = entities.find(node.id()); it == entities.end()) 
        {
            auto y = g->get_attrib_by_name<height_att>(node);
            auto x = g->get_attrib_by_name<width_att>(node);
            auto z = g->get_attrib_by_name<depth_att>(node);
            auto path = g->get_attrib_by_name<path_att>(node);
            auto texture = g->get_attrib_by_name<texture_att>(node);


            auto mat = inner->get_transformation_matrix("world", node.name());

            if (((x.has_value() && y.has_value() && z.has_value()) || path.has_value()) && mat.has_value()) {

                auto *Entity = new Qt3DCore::QEntity(rootEntity);
                entities.emplace(node.id(), Entity);
                
                Entity->setObjectName(QString(node.name().data()));
                QVector3D scale (1, 1, 1);
                Qt3DRender::QMaterial *material = nullptr;
                
                if (texture.has_value()) {
                    std::string_view str((*texture).get());
                    if  (!str.empty()){
                        if (str[0] == '#')
                        {
                            material = color_material(QColor(QString(str.data())));
                        } else 
                        {
                            qInfo() << "[NOT SUPPORTED] Other material. should load texture.";
                        }
                    } else {
                        material = color_material(QColor("gray"));
                    }
                } else {
                    material = color_material(QColor("gray"));
                }
                
                auto plane_mesh = [&](auto& x, auto& y, auto& z) {

                    auto *planeGeom = new Qt3DExtras::QCuboidGeometry;//QCuboidGeom;
                    auto *planeMesh = new Qt3DRender::QGeometryRenderer(Entity);

                    auto n_x = (*x / 10.f != 0) ?  *x / 10.f : 0.00001;
                    auto n_y = (*y / 10.f != 0) ?  *y / 10.f : 0.00001;
                    auto n_z = (*z / 10.f != 0) ?  *z / 10.f : 0.00001;

                    qInfo() << "Create mesh "<< QString::fromStdString(node.name()) <<" " << n_x << " " << n_y << " " << n_z;

                    planeGeom->setXExtent(n_x);
                    planeGeom->setYExtent(n_y);
                    planeGeom->setZExtent(n_z);
                    planeGeom->setXYMeshResolution(QSize(2, 2));
                    planeGeom->setXZMeshResolution(QSize(2, 2));
                    planeGeom->setYZMeshResolution(QSize(2, 2));
                    
                    planeMesh->setGeometry(planeGeom);
                    Entity->addComponent(planeMesh);
                    Entity->addComponent(globalLayer);

                };

                if (auto [path, scalex, scaley, scalez] = g->get_attribs_by_name<DSR::Node, path_att, scalex_att, scaley_att, scalez_att>(node); path.has_value())
                {

                    
                    auto *mesh = new Qt3DRender::QMesh(Entity);

                    QUrl meshpath;
                    QString pathqstr (path->data());
                    //TODO: support .ive y .3ds
                    pathqstr.replace(".ive", ".obj");
                    pathqstr.replace(".3ds", ".obj");
                    QFileInfo check_file(pathqstr);
                    pathqstr = check_file.canonicalFilePath();
                    meshpath.setScheme("file");
                    meshpath.setPath(pathqstr);
                    mesh->setSource(meshpath);

                    std::cout << pathqstr.toStdString() << ", children nodes=" << mesh->childNodes().count() << " file exist?: "<< std::boolalpha<< (check_file.exists() && check_file.isFile())  << std::endl;

                    if (!pathqstr.endsWith(".obj") )
                    {
                        qInfo() << "no obj file: "<< QString::fromStdString(node.name()) <<" " << *x << " " << *y << " " << *z;
                        plane_mesh(x, y, z);
                    }
                    else {
                        std::cout << "Set mesh. scale  " <<scalex.value_or(1.0)<< " " << scaley.value_or(1.0)<< " " << scalez.value_or(1.0) << std::endl;
                        Entity->addComponent(mesh);
                        Entity->addComponent(globalLayer);

                        scale = QVector3D(scalex.value_or(1.0)/10, scaley.value_or(1.0)/10, scalez.value_or(1.0)/10);
                        scale = QVector3D(scalex.value_or(1.0)/10, scaley.value_or(1.0)/10, scalez.value_or(1.0)/10);
                    }

                } else {
                    qInfo() << "no path in object "<< QString::fromStdString(node.name()) <<" " << *x << " " << *y << " " << *z;
                    plane_mesh(x, y, z);
                }

                Qt3DCore::QTransform *planeTransform = new Qt3DCore::QTransform;
                planeTransform->setScale3D(scale);
                auto tr = mat->translation();
                Eigen::Matrix<float, 3, 3> mcopy = mat->rotation().cast<float>();
                QMatrix3x3 rot(mcopy.data());
                planeTransform->setTranslation(QVector3D(tr.x() / 10, tr.y() / 10, tr.z() / 10));
                planeTransform->setRotation(QQuaternion::fromRotationMatrix(rot));
                
                Entity->addComponent(material);
                Entity->addComponent(planeTransform);                        
            }
        } else {

            auto Entity = it->second;
            auto mat = inner->get_transformation_matrix("world", node.name());

            auto componentList = Entity->components();
            auto c = std::find_if(componentList.begin(), componentList.end(),
                                    [](auto &p) {
                                        return qobject_cast<Qt3DCore::QTransform *>(p);
                                    });
            auto transform = qobject_cast<Qt3DCore::QTransform *>(*c);

            auto tr = mat->translation();
            Eigen::Matrix<float, 3, 3> mcopy = mat->rotation().cast<float>();
            QMatrix3x3 rot(mcopy.data());
            transform->setTranslation(QVector3D(tr.x() / 10, tr.y() / 10, tr.z() / 10));
            transform->setRotation(QQuaternion::fromRotationMatrix(rot));
        }
    }


    void QT3DViewer::update_edge(uint64_t from, uint64_t to, const std::string& type)
    {
        if (type == "RT"sv && g->get_edge(from, to, type).has_value())
        {
            if (auto node = g->get_node(to); node.has_value())
            {
                update_qt3d_entity(*node);
                //TODO: update_qt3d_entity should perform an update if the node already exists.
            }
        }
    }

    void QT3DViewer::delete_edge(uint64_t from, uint64_t to, const std::string& type)
    {
        delete_node(to);
    }

    void QT3DViewer::delete_node(uint64_t id)
    {
        auto node = entities.extract(id);
        if (!node.empty()){
            auto entity = node.mapped();
            entity->setParent((Qt3DCore::QNode*)nullptr);
            entity->setEnabled(false);
            entity->deleteLater();
        }
    }

    void QT3DViewer::update_node(uint64_t id, const std::string& type)
    {
        if (type == "plane"sv || type == "mesh"sv) //TODO: Human? Que tenga un mesh
        {
            if (auto node = g->get_node(id); node.has_value()){
                update_qt3d_entity(*node);
            } 
        }
    }

}