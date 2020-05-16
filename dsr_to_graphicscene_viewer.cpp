#include "dsr_to_graphicscene_viewer.h"

using namespace DSR ;

DSRtoGraphicsceneViewer::DSRtoGraphicsceneViewer(std::shared_ptr<CRDT::CRDTGraph> G_, float scaleX, float scaleY, QWidget *parent) : 
                        QWidget(parent)
{
    G = G_;
    m_scaleX = scaleX;
    m_scaleY = scaleY;
    this->resize(parent->width(), parent->height());
	scene.setItemIndexMethod(QGraphicsScene::NoIndex);
	scene.setSceneRect(-5000, -5000, 10000, 10000);
    view = new QGraphicsView(parent);
    view->resize(parent->width(), parent->height());
	view->setScene(&scene);
	view->setCacheMode(QGraphicsView::CacheBackground);
	view->setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
	view->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
	view->setRenderHint(QPainter::Antialiasing);
	view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	this->setContextMenuPolicy(Qt::ActionsContextMenu);
	view->scale(scaleX, scaleY);
	view->setMinimumSize(200, 200);
	view->fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
	view->adjustSize();
 	QRect availableGeometry(QApplication::desktop()->availableGeometry());
 	this->move((availableGeometry.width() - width()) / 2, (availableGeometry.height() - height()) / 2);
	setMouseTracking(true);
    view->viewport()->setMouseTracking(true);

    createGraph();
}

void DSRtoGraphicsceneViewer::createGraph()
{
    try
    {
        auto map = G->getCopy();
		for(const auto &[k, node] : map)
		       add_or_assign_node_slot(k,  node.type());
/*		for(auto node : map) // Aworset
           	for(const auto &[k, edges] : node.second.fano())
			    add_or_assign_edge_slot(edges.from(), edges.to(), edges.type());
*/    }
	catch(const std::exception &e) { std::cout << e.what() << " Error accessing "<< __FUNCTION__<<":"<<__LINE__<< std::endl;}
    innermodel = G->get_inner_api();
}
/*
///////////////////////////////////////////////////////////////////////////////////
void DSRtoGraphicsceneViewer::updateX()
{
    //viewer->frame();
}

void  DSRtoGraphicsceneViewer::setMainCamera(osgGA::TrackballManipulator *manipulator, CameraView pov) const
{
	osg::Quat mRot;

	switch(pov)
	{
        case TOP_POV:
            mRot.makeRotate(-M_PI_2, QVecToOSGVec(QVec::vec3(1,0,0)));
            break;
        case BACK_POV:
            mRot.makeRotate(M_PI_2,  QVecToOSGVec(QVec::vec3(0,0,0)));
            break;
        case FRONT_POV:
            mRot.makeRotate(M_PI,    QVecToOSGVec(QVec::vec3(0,1,0)));
            break;
        case LEFT_POV:
            mRot.makeRotate(M_PI_2,  QVecToOSGVec(QVec::vec3(0,-1,0)));
            break;
        case RIGHT_POV:
            mRot.makeRotate(M_PI_2,  QVecToOSGVec(QVec::vec3(0,1,0)));
            break;
        default:
            qFatal("InnerModelViewer: invalid POV.");
	}
	manipulator->setRotation(mRot);
}
*/

//////////////////////////////////////////////////////////////////////////////////////
///// SLOTS
//////////////////////////////////////////////////////////////////////////////////////

void DSRtoGraphicsceneViewer::add_or_assign_node_slot(const std::int32_t id, const std::string &type)
{
     qDebug() << __FUNCTION__ ;
     qDebug()<<"*************************";
     
     auto node = G->get_node(id);
     std::cout << node.value().name() << " " << node.value().id() << std::endl;
     auto tipoIM = G->get_attrib_by_name<std::string>(node.value(), "imType");
     std::cout << tipoIM.value() << std::endl;
     if(node.has_value() and tipoIM.has_value())
     {
        if( tipoIM.value() == "plane")
         add_or_assign_box(node.value());
//        if( tipoIM.value() == "mesh")
//         add_or_assign_mesh(node.value());
     }
}
void DSRtoGraphicsceneViewer::add_or_assign_edge_slot(const std::int32_t from, const std::int32_t to, const std::string& type)
{
     qDebug() << __FUNCTION__ ;
}

void DSRtoGraphicsceneViewer::add_or_assign_box(Node &node)
{
    qDebug() << __FUNCTION__ ;
    auto parent = G->get_attrib_by_name<std::int32_t>(node, "parent");
    if(parent.has_value())
        std::cout << "parent: "<< parent.value() << std::endl;
    auto n_color = G->get_attrib_by_name<std::string>(node, "color");
    QString color = "red";
    if(n_color.has_value())
        color = QString::fromStdString(n_color.value());
    std::cout << "color:" << n_color.value() << std::endl;
    auto filename = G->get_attrib_by_name<std::string>(node, "texture");
    if(filename.has_value()) std::cout <<"filname: " << filename.value() << std::endl;
    auto width = G->get_attrib_by_name<std::int32_t>(node, "width");
    if(width.has_value()) std::cout << "width:" << width.value() << std::endl;
    auto height = G->get_attrib_by_name<std::int32_t>(node, "height");
    if(height.has_value()) std::cout << "height: " << height.value() << std::endl;
    auto depth = G->get_attrib_by_name<std::int32_t>(node, "depth");
    if(depth.has_value()) std::cout <<"depth: " << depth.value() << std::endl;
    auto nx = G->get_attrib_by_name<std::int32_t>(node, "nx");
    if(nx.has_value()) std::cout <<"nx: "<< nx.value() << std::endl;
    auto ny = G->get_attrib_by_name<std::int32_t>(node, "ny");
    if(ny.has_value()) std::cout <<"ny:" << ny.value() << std::endl;
    auto nz = G->get_attrib_by_name<std::int32_t>(node, "nz");
    if(nz.has_value()) std::cout <<"nz:" << nz.value() << std::endl;
    auto px = G->get_attrib_by_name<std::int32_t>(node, "px");
    if(px.has_value()) std::cout <<"px:"<< px.value() << std::endl;
    auto py = G->get_attrib_by_name<std::int32_t>(node, "py");
    if(py.has_value()) std::cout <<"py:"<< py.value() << std::endl;
    auto pz = G->get_attrib_by_name<std::int32_t>(node, "pz");
    if(pz.has_value()) std::cout <<"pz:"<< pz.value() << std::endl;
    auto transparency = G->get_attrib_by_name<std::float_t>(node, "transparency");     
    if(transparency.has_value()) std::cout <<"transparency: "<< transparency.value() << std::endl;
    
    //we are in bussines
 //   bool constantColor = false;
 //   if (filename.value().size() == 7 and filename.value()[0] == '#')
 //           constantColor = true;
    
    //check if has required values
    if(px.has_value() and py.has_value() and pz.has_value() and width.has_value() and height.has_value())
    {
qDebug()<<__LINE__;
        // get transfrom to world => to get correct position
        std::optional<QVec> pose = innermodel->transform("world", QVec::vec3(px.value(), py.value(), pz.value()), QString::fromStdString(node.name()));
qDebug()<<__LINE__;        
        if (pose.has_value())
{qDebug()<<__LINE__;        
            scene.addRect(pose.value().x(), pose.value().z(), width.value(), height.value(), QPen(QColor(color)), QBrush(QColor(color)));
}
    }
    else
    {
        std::cout<<"Error drawing "<< node<< " some required attribs has no value"<<std::endl;
    }
    
//    box->setPos(0);
//	box->setRotation(0);
//		boxes.push_back(box);

}
/*
void  DSRtoGraphicsceneViewer::add_or_assign_mesh(Node &node)
{   
    auto parent = G->get_attrib_by_name<std::int32_t>(node, "parent");
    if(parent.has_value()) std::cout << parent.value() << std::endl;
    auto color = G->get_attrib_by_name<std::string>(node, "color");
    if(color.has_value()) std::cout << color.value() << std::endl;
    auto filename = G->get_attrib_by_name<std::string>(node, "path");
    if(filename.has_value()) std::cout << filename.value() << std::endl;
    auto scalex = G->get_attrib_by_name<std::int32_t>(node, "scalex");
    if(scalex.has_value()) std::cout << scalex.value() << std::endl;
    auto scaley = G->get_attrib_by_name<std::int32_t>(node, "scaley");
    if(scaley.has_value()) std::cout << scaley.value() << std::endl;
    auto scalez = G->get_attrib_by_name<std::int32_t>(node, "scalez");
    if(scalez.has_value()) std::cout << scalez.value() << std::endl;
    auto rx = G->get_attrib_by_name<std::int32_t>(node, "rx");
    if(rx.has_value()) std::cout << rx.value() << std::endl;
    auto ry = G->get_attrib_by_name<std::int32_t>(node, "ry");
    if(ry.has_value()) std::cout << ry.value() << std::endl;
    auto rz = G->get_attrib_by_name<std::int32_t>(node, "rz");
    if(rz.has_value()) std::cout << rz.value() << std::endl;
    auto tx = G->get_attrib_by_name<std::int32_t>(node, "tx");
    if(tx.has_value()) std::cout << tx.value() << std::endl;
    auto ty = G->get_attrib_by_name<std::int32_t>(node, "ty");
    if(ty.has_value()) std::cout << ty.value() << std::endl;
    auto tz = G->get_attrib_by_name<std::int32_t>(node, "tz");
    if(tz.has_value()) std::cout << tz.value() << std::endl;

    osg::ref_ptr<osg::MatrixTransform> mt = new osg::MatrixTransform;
    // //if (parent) parent->addChild(mt);
    RTMat rtmat = RTMat();
    rtmat.setR (rx.value_or(0), ry.value_or(0), rz.value_or(0));
    rtmat.setTr(tx.value_or(0), ty.value_or(0), tz.value_or(0));
    mt->setMatrix(QMatToOSGMat4(rtmat));
    osg::ref_ptr<osg::MatrixTransform> smt = new osg::MatrixTransform; 		
    smt->setMatrix(osg::Matrix::scale(scalex.value_or(1),scaley.value_or(1),scalez.value_or(1)));
    mt->addChild(smt);
    // meshHash[mesh->id].osgmeshPaths = mt;
    osg::ref_ptr<osg::Node> osgMesh = osgDB::readNodeFile(filename.value());
    if (!osgMesh)
        throw  "Could not find nesh file " + filename.value();
    osg::ref_ptr<osg::PolygonMode> polygonMode = new osg::PolygonMode();
    polygonMode->setMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::FILL);
    osgMesh->getOrCreateStateSet()->setAttributeAndModes(polygonMode, osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);
    osgMesh->getOrCreateStateSet()->setMode( GL_RESCALE_NORMAL, osg::StateAttribute::ON );
    smt->addChild(osgMesh);
    osgObjectsMap.insert_or_assign(node.id(), smt);
    if (std::optional<Node> parent_node = G->get_node(parent.value()); parent_node.has_value())
        std::get<osg::Group*>(osgObjectsMap.at(parent_node.value().id()))->addChild(smt);
    else 
        root->addChild(smt);

    // //meshHash[mesh->id].osgmeshes = osgMesh;
    // // //meshHash[mesh->id].meshMts= mt;
    // // //osgmeshmodes[mesh->id] = polygonMode;
    // smt->addChild(osgMesh);
}

/////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////

void DSRtoGraphicsceneViewer::paintGL() 
{
    _mViewer->frame();
}
*/

/*
void DSRtoGraphicsceneViewer::initializeGL()
{
    //osg::Geode* geode = dynamic_cast<osg::Geode*>(_mViewer->getSceneData());
    //osg::StateSet* stateSet = geode->getOrCreateStateSet();
    // osg::Material* material = new osg::Material;
    // material->setColorMode( osg::Material::AMBIENT_AND_DIFFUSE );
    // stateSet->setAttributeAndModes( material, osg::StateAttribute::ON );
    // stateSet->setMode( GL_DEPTH_TEST, osg::StateAttribute::ON );
}     

void DSRtoGraphicsceneViewer::mouseMoveEvent(QMouseEvent* event)
{
    this->getEventQueue()->mouseMotion(event->x()*m_scaleX, event->y()*m_scaleY);
}

void DSRtoGraphicsceneViewer::mousePressEvent(QMouseEvent* event)
{
    unsigned int button = 0;
    switch (event->button()){
    case Qt::LeftButton:
        button = 1;
        break;
    case Qt::MiddleButton:
        button = 2;
        break;
    case Qt::RightButton:
        button = 3;
        break;
    default:
        break;
    }
    this->getEventQueue()->mouseButtonPress(event->x()*m_scaleX, event->y()*m_scaleY, button);
}

void DSRtoGraphicsceneViewer::mouseReleaseEvent(QMouseEvent* event)
{
    unsigned int button = 0;
    switch (event->button()){
    case Qt::LeftButton:
        button = 1;
        break;
    case Qt::MiddleButton:
        button = 2;
        break;
    case Qt::RightButton:
        button = 3;
        break;
    default:
        break;
    }
    this->getEventQueue()->mouseButtonRelease(event->x()*m_scaleX, event->y()*m_scaleY, button);
}

void DSRtoGraphicsceneViewer::own_wheelEvent(QWheelEvent* event)
{
    qDebug()<<"wheel";
    const QGraphicsView::ViewportAnchor anchor = view->transformationAnchor();
	view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	int angle = event->angleDelta().y();
	qreal factor;
	if (angle > 0) 
	{
		factor = 1.1;
		QRectF r = scene.sceneRect();
		scene.setSceneRect(r);
	}
	else
	{
		factor = 0.9;
		QRectF r = scene.sceneRect();
		scene.setSceneRect(r);
	}
	view->scale(factor, factor);
	view->setTransformationAnchor(anchor);
}

bool DSRtoGraphicsceneViewer::event(QEvent* event)
{
    bool handled = QOpenGLWidget::event(event);
    this->update();
    return handled;
}

osgGA::EventQueue* DSRtoGraphicsceneViewer::getEventQueue() const 
{
    osgGA::EventQueue* eventQueue = _mGraphicsWindow->getEventQueue();
    // auto center = manipulator->getCenter();
    // qDebug() << center.x() << center.y() << center.z() ;
    return eventQueue;
}
*/