#include "dsr_to_osg_viewer.h"
#include <osg/Camera>
#include <osg/ShapeDrawable>
#include <osg/StateSet>
#include <osg/Material>
#include <osgGA/EventQueue>
#include <osgGA/TrackballManipulator>
#include <QMouseEvent>
 
using namespace DSR;

DSRtoOSGViewer::DSRtoOSGViewer(std::shared_ptr<CRDT::CRDTGraph> G_, float scaleX, float scaleY, QWidget *parent) : 
                        QOpenGLWidget(parent), 
                        _mGraphicsWindow(new osgViewer::GraphicsWindowEmbedded(this->x(), this->y(), this->width(), this->height())), 
                        _mViewer(new osgViewer::Viewer), m_scaleX(scaleX), m_scaleY(scaleY)
{
    G = G_;
    this->resize(parent->width(), parent->height());
    root = new osg::Group();
    osg::Camera* camera = new osg::Camera;
    camera->setViewport( 0, 0, this->width(), this->height() );
    camera->setClearColor( osg::Vec4( 0.9f, 0.9f, 1.f, 1.f ) );
    float aspectRatio = static_cast<float>( this->width()) / static_cast<float>( this->height() );
    camera->setProjectionMatrixAsPerspective(55.0f, aspectRatio, 0.000001, 100000.0);
    //camera->setProjectionMatrixAsPerspective( 30.f, aspectRatio, 1.f, 1000.f );
    camera->setGraphicsContext( _mGraphicsWindow );
    _mViewer->setCamera(camera);
    manipulator = new osgGA::TrackballManipulator;
    manipulator->setAllowThrow( false );
    this->setMouseTracking(true);
    // osg::Vec3d eye(osg::Vec3(1.,1.,1000.));
    // osg::Vec3d center(osg::Vec3(0.,0.,-0.));
    // osg::Vec3d up(osg::Vec3(0.,1.,0.));
    // manipulator->setHomePosition(eye, center, up, true);
    // manipulator->setByMatrix(osg::Matrixf::lookAt(eye,center,up));
    // manipulator->setHomePosition(osg::Vec3(0,0,0),osg::Vec3(0.f,0.,-40.),osg::Vec3(0.0f,1.f,0.0f), false);
    _mViewer->setCameraManipulator(manipulator);
    _mViewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
	
 	//global stateset
	osg::StateSet *globalStateSet = new osg::StateSet;
	globalStateSet->setGlobalDefaults();
	globalStateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);

	// // enable lighting
	globalStateSet->setMode(GL_LIGHTING, osg::StateAttribute::ON);
	// osg::Light* light = _mViewer->getLight();
	// light->setAmbient(  osg::Vec4( 0.4f,    0.4f, 0.4f,  1.f ));
	// light->setDiffuse(  osg::Vec4( 0.8f,    0.8f, 0.8f,  1.f ));
	// light->setSpecular( osg::Vec4( 0.2f,    0.2f, 0.2f,  1.f ));
	// light->setPosition( osg::Vec4( 0.0f, 3000.0f, 0.0f,  1.f));
    // light->setDirection(osg::Vec3(0.0f, -1.0f, 0.0f));
	// osg::ref_ptr<osg::LightSource> lightSource = new osg::LightSource;
	// lightSource->setLight(light);
	// lightSource->setLocalStateSetModes(osg::StateAttribute::ON);
	// lightSource->setStateSetModes(*globalStateSet,osg::StateAttribute::ON);
	//root->addChild(lightSource.get() );

    //add_cylinder(root);
	_mViewer->setSceneData( root.get());
	
    createGraph();
    //connect(&timer, &QTimer::timeout, this, &DSRtoOSGViewer::updateX);
    //timer.start(100);
    
    //connect(G.get(), &CRDT::CRDTGraph::update_node_signal, this, &DSRtoOSGViewer::add_or_assign_node_slot);
	//connect(G.get(), &CRDT::CRDTGraph::update_edge_signal, this, &DSRtoOSGViewer::add_or_assign_edge_slot);

	//connect(G.get(), &CRDT::CRDTGraph::del_edge_signal, this, &DSRtoOSGViewer::delEdgeSLOT);
	//connect(G.get(), &CRDT::CRDTGraph::del_node_signal, this, &DSRtoOSGViewer::delNodeSLOT);

    _mViewer->realize();

    setMainCamera(manipulator, TOP_POV);
   
}

void DSRtoOSGViewer::createGraph()
{
    try
    {
        auto map = G->getCopy();
		for(const auto &[k, node] : map)
		    add_or_assign_node_slot(k,  node.type());
        for(const auto &[k, node] : map)
            for(const auto &[ek, edge]: node.fano())
		            add_or_assign_edge_slot(edge.from(), edge.to(), edge.type());
		//for(auto node : map) // Aworset
        //   	for(const auto &[k, edges] : node.second.fano())
		//	    add_or_assign_edge_slot(edges.from(), edges.to(), edges.type());
    }
	catch(const std::exception &e) { std::cout << e.what() << " Error accessing "<< __FUNCTION__<<":"<<__LINE__<< std::endl;}
}

///////////////////////////////////////////////////////////////////////////////////
void DSRtoOSGViewer::updateX()
{
    //viewer->frame();
}

void  DSRtoOSGViewer::setMainCamera(osgGA::TrackballManipulator *manipulator, CameraView pov) const
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


//////////////////////////////////////////////////////////////////////////////////////
///// SLOTS
//////////////////////////////////////////////////////////////////////////////////////

void DSRtoOSGViewer::add_or_assign_node_slot(const std::int32_t id, const std::string &type)
{
     qDebug() << __FILE__ << __FUNCTION__ ;
     
     auto node = G->get_node(id);
     std::cout << node.value().name() << " " << node.value().id() << std::endl;
     auto tipoIM = G->get_attrib_by_name<std::string>(node.value(), "imType");
     std::cout << tipoIM.value() << std::endl;
     if(node.has_value() and tipoIM.has_value())
     {
        if( tipoIM.value() == "plane")
         add_or_assign_box(node.value());
        if( tipoIM.value() == "mesh")
         add_or_assign_mesh(node.value());
     }
}
void DSRtoOSGViewer::add_or_assign_edge_slot(const std::int32_t from, const std::int32_t to, const std::string& type)
{
     qDebug() << __FILE__ << __FUNCTION__ ;
}

void DSRtoOSGViewer::add_or_assign_box(Node &node)
{
    qDebug() << __FUNCTION__ ;
    std::cout << node.name() << " " << node.id() << std::endl;
    auto texture = G->get_attrib_by_name<std::string>(node, "texture");
    if(texture.has_value()) std::cout << texture.value() << std::endl;
    auto parent = G->get_attrib_by_name<std::int32_t>(node, "parent");
    if(parent.has_value()) std::cout << parent.value() << std::endl;
    auto height = G->get_attrib_by_name<std::int32_t>(node, "height");
    if(height.has_value()) std::cout << height.value() << std::endl;
    auto width = G->get_attrib_by_name<std::int32_t>(node, "width");
    if(width.has_value()) std::cout << height.value() << std::endl;
    auto depth = G->get_attrib_by_name<std::int32_t>(node, "depth");
    if(depth.has_value()) std::cout << depth.value() << std::endl;
    auto nx = G->get_attrib_by_name<std::int32_t>(node, "nx");
    if(nx.has_value()) std::cout << nx.value() << std::endl;
    auto ny = G->get_attrib_by_name<std::int32_t>(node, "ny");
    if(ny.has_value()) std::cout << ny.value() << std::endl;
    auto nz = G->get_attrib_by_name<std::int32_t>(node, "nz");
    if(nz.has_value()) std::cout << nz.value() << std::endl;
    auto px = G->get_attrib_by_name<std::int32_t>(node, "px");
    if(px.has_value()) std::cout << px.value() << std::endl;
    auto py = G->get_attrib_by_name<std::int32_t>(node, "py");
    if(py.has_value()) std::cout << py.value() << std::endl;
    auto pz = G->get_attrib_by_name<std::int32_t>(node, "pz");
    if(pz.has_value()) std::cout << pz.value() << std::endl;
    auto transparency = G->get_attrib_by_name<std::float_t>(node, "transparency");     
    if(transparency.has_value()) std::cout << transparency.value() << std::endl;
    
    //we are in bussines
    bool constantColor = false;
    if (texture.value().size() == 7 and texture.value()[0] == '#')
            constantColor = true;
    // Open image
    osg::ref_ptr<osg::TessellationHints> hints;
    osg::Image *image;
    if (texture.value().size()>0 and not constantColor)
    {
        if( image = osgDB::readImageFile(texture.value()), image == nullptr)
            throw std::runtime_error("Couldn't load texture from file: " + texture.value());
    }   

    hints = new osg::TessellationHints;
    hints->setDetailRatio(2.0f);
    osg::ref_ptr<osg::Box> box = new osg::Box(QVecToOSGVec(QVec::vec3(px.value()/100,py.value()/100,pz.value()/100)), width.value()/100, height.value()/100, depth.value()/100);
    osg::Matrix r;
	r.makeRotate(osg::Vec3(0, 0, 1), QVecToOSGVec(QVec::vec3(nx.value(),ny.value(),nz.value())));
	osg::Matrix t;
	t.makeTranslate(QVecToOSGVec(QVec::vec3(px.value()/100,py.value()/100,pz.value()/100)));
    osg::Quat qr; qr.set(r*t);
	box->setRotation(qr);
    auto plane_drawable = new osg::ShapeDrawable(box, hints);
    plane_drawable->setColor(htmlStringToOsgVec4(texture.value_or("#FF0000")));
    osg::Geode* geode = new osg::Geode;
    geode->addDrawable(plane_drawable);

    if (not constantColor)
    {
        // Texture
        auto texture = new osg::Texture2D;
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
        texture->setWrap(osg::Texture::WRAP_R, osg::Texture::REPEAT);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
        texture->setImage(image);
        //texture->setDataVariance(Object::DYNAMIC);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
        texture->setWrap(osg::Texture::WRAP_R, osg::Texture::REPEAT);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
        texture->setTextureWidth(1);
        texture->setTextureHeight(1);
        texture->setResizeNonPowerOfTwoHint(false);

        // Material
        osg::ref_ptr<osg::Material> material = new osg::Material();
        material->setTransparency( osg::Material::FRONT_AND_BACK, transparency.value_or(0));
        material->setEmission(osg::Material::FRONT, osg::Vec4(0.8, 0.8, 0.8, 0.5));
        // Assign the material and texture to the plane
        osg::StateSet *sphereStateSet = geode->getOrCreateStateSet();
        sphereStateSet->ref();
        sphereStateSet->setAttribute(material);
        sphereStateSet->setTextureMode(0, GL_TEXTURE_GEN_R, osg::StateAttribute::ON);
        sphereStateSet->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
    }
    osgObjectsMap.insert_or_assign(node.id(), geode);
    if (std::optional<Node> parent_node = G->get_node(parent.value()); parent_node.has_value())
        std::get<osg::Group*>(osgObjectsMap.at(parent_node.value().id()))->addChild(geode);
    else 
        root->addChild(geode);

     //if (std::optional<Node> parent_node = G->get_node(parent.value()); parent_node.has_value())
        //    std::get<osg::Group*>(osgObjectsMap.at(parent_node.value().id()))->addChild(geode);
        
        // see if there are RT edges
        
        // auto rt_edges_list = node.value().get_edges_RT();
        // for(auto &rt_edge : rt_edges_list)
        // {
        //     osg::ref_ptr<osg::MatrixTransform> mt = new osg::MatrixTransform;
        //     //setOSGMatrixTransformForPlane(mt, plane);
        //     osgTransformMap.insert_or_assign(std::make_pair(rt_edge.from(), er_edge.to()), mt);
        //     osg_obj->addChild( osgObjectMap.at(rt_edge.to()) );
        // }
	
        //setOSGMatrixTransformForPlane(mt, plane);
}

void  DSRtoOSGViewer::add_or_assign_mesh(Node &node)
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
    smt->setMatrix(osg::Matrix::scale(scalex.value_or(1)/100,scaley.value_or(1)/100,scalez.value_or(1)/100));
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

osg::Vec3 DSRtoOSGViewer::QVecToOSGVec(const QVec &vec) const 
{
	return osg::Vec3(vec(0), vec(1), -vec(2));
}

osg::Vec4 DSRtoOSGViewer::htmlStringToOsgVec4(const std::string &color)
{
	QString red   = QString("00");
	QString green = QString("00");
	QString blue  = QString("00");
	bool ok;
	red[0]   = color[1]; red[1]   = color[2];
	green[0] = color[3]; green[1] = color[4];
	blue[0]  = color[5]; blue[1]  = color[6];
	return osg::Vec4(float(red.toInt(&ok, 16))/255., float(green.toInt(&ok, 16))/255., float(blue.toInt(&ok, 16))/255., 0.f);
}

osg::Matrix  DSRtoOSGViewer::QMatToOSGMat4(const RTMat &nodeB)
{
	QVec angles = nodeB.extractAnglesR();
	QVec t = nodeB.getTr();
	RTMat node = RTMat(-angles(0), -angles(1), angles(2), QVec::vec3(t(0), t(1), -t(2)));

	return osg::Matrixd( node(0,0), node(1,0), node(2,0), node(3,0),
	                     node(0,1), node(1,1), node(2,1), node(3,1),
	                     node(0,2), node(1,2), node(2,2), node(3,2),
	                     node(0,3), node(1,3), node(2,3), node(3,3) );
}

////////////////////////////////////////////////////////////////

void DSRtoOSGViewer::paintGL() 
{
    _mViewer->frame();
}

void DSRtoOSGViewer::resizeGL( int width, int height ) 
{
    this->getEventQueue()->windowResize(this->x()*m_scaleX, this->y() * m_scaleY, width*m_scaleX, height*m_scaleY);
    _mGraphicsWindow->resized(this->x()*m_scaleX, this->y() * m_scaleY, width*m_scaleX, height*m_scaleY);
    osg::Camera* camera = _mViewer->getCamera();
    camera->setViewport(0, 0, this->width()*m_scaleX, this->height()* m_scaleY);
}

void DSRtoOSGViewer::initializeGL()
{
    //osg::Geode* geode = dynamic_cast<osg::Geode*>(_mViewer->getSceneData());
    //osg::StateSet* stateSet = geode->getOrCreateStateSet();
    // osg::Material* material = new osg::Material;
    // material->setColorMode( osg::Material::AMBIENT_AND_DIFFUSE );
    // stateSet->setAttributeAndModes( material, osg::StateAttribute::ON );
    // stateSet->setMode( GL_DEPTH_TEST, osg::StateAttribute::ON );
}     

void DSRtoOSGViewer::mouseMoveEvent(QMouseEvent* event)
{
    this->getEventQueue()->mouseMotion(event->x()*m_scaleX, event->y()*m_scaleY);
}

void DSRtoOSGViewer::mousePressEvent(QMouseEvent* event)
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

void DSRtoOSGViewer::mouseReleaseEvent(QMouseEvent* event)
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

void DSRtoOSGViewer::wheelEvent(QWheelEvent* event)
{
    qDebug() << "SHIT";
    int delta = event->delta();
    osgGA::GUIEventAdapter::ScrollingMotion motion = delta > 0 ?
                osgGA::GUIEventAdapter::SCROLL_UP : osgGA::GUIEventAdapter::SCROLL_DOWN;
    this->getEventQueue()->mouseScroll(motion);
}

bool DSRtoOSGViewer::event(QEvent* event)
{
    bool handled = QOpenGLWidget::event(event);
    this->update();
    return handled;
}

osgGA::EventQueue* DSRtoOSGViewer::getEventQueue() const 
{
    osgGA::EventQueue* eventQueue = _mGraphicsWindow->getEventQueue();
    // auto center = manipulator->getCenter();
    // qDebug() << center.x() << center.y() << center.z() ;
    return eventQueue;
}