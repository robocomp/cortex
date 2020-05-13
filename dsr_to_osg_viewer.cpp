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
    //manipulator->setHomePosition(osg::Vec3(0,0,0),osg::Vec3(0.f,0.,-40.),osg::Vec3(0.0f,1.f,0.0f), false);
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
    //if (viewer->size() != frameOSG->size())
	//		viewer->setFixedSize(frameOSG->width(), frameOSG->height());
}

void DSRtoOSGViewer::createGraph()
{
    try
    {
        auto map = G->getCopy();
		for(const auto &[k, node] : map)
		       add_or_assign_node_slot(k,  node.type());
		for(auto node : map) // Aworset
           	for(const auto &[k, edges] : node.second.fano())
			    add_or_assign_edge_slot(edges.from(), edges.to(), edges.type());
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
     auto name = G->get_attrib_by_name<std::string>(node.value(), "imName");
     std::cout << name.value() << std::endl;
     auto tipoIM = G->get_attrib_by_name<std::string>(node.value(), "imType");
     std::cout << tipoIM.value() << std::endl;
     auto parent = G->get_attrib_by_name<std::int32_t>(node.value(), "parent");
     std::cout << parent.value() << std::endl;
     auto color = G->get_attrib_by_name<std::string>(node.value(), "color");
     std::cout << color.value() << std::endl;
     auto filename = G->get_attrib_by_name<std::string>(node.value(), "texture");
     if(filename.has_value()) std::cout << filename.value() << std::endl;
     auto width = G->get_attrib_by_name<std::float_t>(node.value(), "width");
     if(width.has_value()) std::cout << width.value() << std::endl;
     auto height = G->get_attrib_by_name<std::float_t>(node.value(), "height");
     if(height.has_value()) std::cout << height.value() << std::endl;
     auto depth = G->get_attrib_by_name<std::float_t>(node.value(), "depth");
     if(depth.has_value()) std::cout << depth.value() << std::endl;
     auto transparency = G->get_attrib_by_name<std::float_t>(node.value(), "transparency");     
     if(transparency.has_value()) std::cout << transparency.value() << std::endl;
     
     if(node.has_value() and tipoIM.has_value() and tipoIM.value() == "plane")
     {
        //we are in bussines
	    bool constantColor = false;
        if (filename.value().size() == 7 and filename.value()[0] == '#')
                constantColor = true;
	    // Open image
	    osg::ref_ptr<osg::TessellationHints> hints;
        osg::Image *image;
	    if (filename.value().size()>0 and not constantColor)
			if( image = osgDB::readImageFile(filename.value()), image == nullptr)
				throw std::runtime_error("Couldn't load texture from file: " + filename.value());
	
        hints = new osg::TessellationHints;
	    hints->setDetailRatio(2.0f);
	    osg::ref_ptr<osg::Box> myBox = new osg::Box(QVecToOSGVec(QVec::vec3(0,0,0)), width.value()/100, height.value()/100, depth.value()/100);
        auto plane_drawable = new osg::ShapeDrawable(myBox, hints);
	    plane_drawable->setColor(htmlStringToOsgVec4(color.value()));
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
        }
        
        osgObjectsMap.insert_or_assign(node.value().id(), geode);
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
     
}
void DSRtoOSGViewer::add_or_assign_edge_slot(const std::int32_t from, const std::int32_t to, const std::string& type)
{
     qDebug() << __FILE__ << __FUNCTION__ ;
}

void DSRtoOSGViewer::add_plane()
{

}

void DSRtoOSGViewer::add_cylinder(osg::ref_ptr<osg::Group> root)
{
    osg::Cylinder* cylinder  = new osg::Cylinder( osg::Vec3( 0.f, 0.f, 0.f ), 5.25f, 3.5f );
    osg::ShapeDrawable* sd = new osg::ShapeDrawable( cylinder );
    sd->setColor( osg::Vec4( 0.8f, 0.5f, 0.2f, 1.f ) );
    osg::Geode* geode = new osg::Geode;
    geode->addDrawable(sd);
    osg::StateSet* stateSet = geode->getOrCreateStateSet();
    osg::Material* material = new osg::Material;
    material->setColorMode( osg::Material::AMBIENT_AND_DIFFUSE );
    stateSet->setAttributeAndModes( material, osg::StateAttribute::ON );
    stateSet->setMode( GL_DEPTH_TEST, osg::StateAttribute::ON );
    root->addChild(geode);
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
////////////////////////////////////////////////////////////////

void DSRtoOSGViewer::paintGL() 
{
    _mViewer->frame();
}

void DSRtoOSGViewer::resizeGL( int width, int height ) 
{
    qDebug() << "SCAKE" << x() << y();
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
    return eventQueue;
}