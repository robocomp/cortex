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
	//_mViewer->setSceneData( root.get());

	root = createGraph();

    analyse_osg_graph(root.get());
    qDebug()<< "End analyse";

    _mViewer->setSceneData( root.get());
	
    //timer.start(100);
    
    //connect(G.get(), &CRDT::CRDTGraph::update_node_signal, this, &DSRtoOSGViewer::add_or_assign_node_slot);
	//connect(G.get(), &CRDT::CRDTGraph::update_edge_signal, this, &DSRtoOSGViewer::add_or_assign_edge_slot);

	//connect(G.get(), &CRDT::CRDTGraph::del_edge_signal, this, &DSRtoOSGViewer::delEdgeSLOT);
	//connect(G.get(), &CRDT::CRDTGraph::del_node_signal, this, &DSRtoOSGViewer::delNodeSLOT);

    _mViewer->realize();

    setMainCamera(manipulator, TOP_POV);
   
}

// We need to go down the tree breadth first
void DSRtoOSGViewer::traverse_RT_tree(const Node& node)
{
    for(auto &edge: G->get_edges_by_type(node, "RT"))
	{
        std::cout << __FUNCTION__ << " edges " << edge.from() << " " << edge.to() << " " << edge.type() << std::endl;
        auto child = G->get_node(edge.to());
        if(child.has_value())
        {
            add_or_assign_edge_slot(node, child.value());
            add_or_assign_node_slot(child.value(),  child.value().type());
            traverse_RT_tree(child.value());
        }
        else
            throw std::runtime_error("Unable to traverse the tree at node: " + std::to_string(edge.to()));
	}
}

osg::ref_ptr<osg::Group> DSRtoOSGViewer::createGraph()
{
    qDebug() << __FUNCTION__ << "Reading graph in OSG Viewer";
    try
    {   std::optional<Node> g_root = G->get_node_root().value();  //HAS TO BE TRANSFORM
        root = new osg::Group();
        osg_map.insert_or_assign(std::make_tuple(g_root.value().id(), g_root.value().id()), root);

        traverse_RT_tree(g_root.value());
        qDebug() << __FUNCTION__ << "Finished reading graph. Waiting for events";
        return root;
    }
	catch(const std::exception &e) { std::cout << e.what() << " Error accessing "<< __FUNCTION__<<":"<<__LINE__<< std::endl;}
    return osg::ref_ptr<osg::Group>();
}

///////////////////////////////////////////////////////////////////////////////////


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

// To insert a node its parent has to be a transform
// Transforms are created form RT edges.

void DSRtoOSGViewer::add_or_assign_node_slot(const Node &node, const std::string &type_)
{
    qDebug() << __FUNCTION__  << "node " << node.id();
     
    auto parent_id = G->get_node_parent(node);
    if (not parent_id.has_value())
        std::runtime_error("Node cannot be inserted without a parent");
    auto parent = G->get_node(parent_id.value());
    auto type = node.type();
    std::cout << __FUNCTION__ << " " << node.name() << " " << node.id() << " " << node.type() << std::endl;
    if( type == "plane")
        add_or_assign_box(node, parent.value());
    else if( type == "mesh")
        add_or_assign_mesh(node, parent.value());
    else if( type == "transform")
        add_or_assign_node_transform(node, parent.value());
    else if( type == "differentialRobot" or type == "laser" or type == "rgbd")
        add_or_assign_node_transform(node, parent.value());
}

void DSRtoOSGViewer::add_or_assign_edge_slot(const Node &from, const Node& to)
{

    qDebug() << __FUNCTION__ << from.id() << "to " << to.id();
    auto edge = G->get_edge_RT(from, to.id());
    auto rtmat = G->get_edge_RT_as_RTMat(edge);
    osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform; 
    transform->setMatrix( QMatToOSGMat4(rtmat) );
    transform->setName(std::to_string(from.id())+"-"+std::to_string(to.id()));
    
    if( auto res = osg_map.find(std::make_tuple(from.id(), from.id())); res != osg_map.end())   
    {
        (*res).second->addChild(transform);
        osg_map.insert_or_assign(std::make_tuple(from.id(), to.id()), transform);
        qDebug() << __FUNCTION__ << "Added transform, node " << to.id() << "parent "  << from.id();
    }
    else
        throw std::runtime_error("Transform: OSG parent not found for " + to.name() + "-" + std::to_string(to.id()));
}

////////////////////////////////////////////////////////////////
/// RT edge Transform
////////////////////////////////////////////////////////////////

void DSRtoOSGViewer::add_or_assign_node_transform(const Node &node, const Node& parent)
{
    qDebug() << __FUNCTION__  << "node " << node.id() << "parent "  << parent.id();
    osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform; 
    osg::Matrix mat;		
    mat.makeIdentity();    
    transform->setMatrix(mat);
    transform->setName(std::to_string(node.id())+"-"+std::to_string(node.id()));
    if( auto res = osg_map.find(std::make_tuple(parent.id(), node.id())); res != osg_map.end())   
    {
        (*res).second->addChild(transform);
        osg_map.insert_or_assign(std::make_tuple(node.id(), node.id()), transform);
        qDebug() << __FUNCTION__ << "Added transform, node " << node.id() << "parent "  << parent.id();
    }
    else
        throw std::runtime_error("Transform: OSG parent not found for " + parent.name() + "-" +std::to_string(parent.id()));
}

void DSRtoOSGViewer::add_or_assign_box(const Node &node, const Node& parent)
{
    qDebug() << __FUNCTION__ << "node " << node.id() << parent.id();
    std::cout << node.name() << " " << node.id() << std::endl;
    auto texture = G->get_attrib_by_name<std::string>(node, "texture");
    if(texture.has_value()) std::cout << texture.value() << std::endl;
    auto height = G->get_attrib_by_name<std::int32_t>(node, "height");
    if(height.has_value()) std::cout << height.value() << std::endl;
    auto width = G->get_attrib_by_name<std::int32_t>(node, "width");
    if(width.has_value()) std::cout << height.value() << std::endl;
    auto depth = G->get_attrib_by_name<std::int32_t>(node, "depth");
    if(depth.has_value()) std::cout << depth.value() << std::endl;
    
    //we are in bussines
    auto textu = texture.value_or("#000000");
    bool constantColor = false;
    if (textu.size() == 7 and textu[0] == '#')
            constantColor = true;
    // Open image
    osg::ref_ptr<osg::TessellationHints> hints;
    osg::Image *image;
    if (textu.size()>0 and not constantColor)
    {
        if( image = osgDB::readImageFile(textu), image == nullptr)
            throw std::runtime_error("Couldn't load texture from file: " + texture.value());
    }   

    hints = new osg::TessellationHints;
    hints->setDetailRatio(2.0f);
    osg::ref_ptr<osg::Box> box = new osg::Box(QVecToOSGVec(QVec::vec3(0,0,0)), width.value()/100, height.value()/100, depth.value()/100);
    auto plane_drawable = new osg::ShapeDrawable(box, hints);
    plane_drawable->setColor(htmlStringToOsgVec4(texture.value_or("#FF0000")));
    osg::Geode* geode = new osg::Geode;
    geode->addDrawable(plane_drawable);
    osg::Group *group = new osg::Group;
    group->setName(std::to_string(node.id())+"-"+std::to_string(node.id()));
    
    if( auto res = osg_map.find(std::make_tuple(parent.id(), node.id())); res != osg_map.end())   
    {
        (*res).second->addChild(group);
        osg_map.insert_or_assign(std::make_tuple(node.id(),node.id()), group);
        qDebug() << __FUNCTION__ << "Added BOX, node " << node.id() << "parent "  << parent.id();
    }
    else
        throw std::runtime_error("Transform: OSG parent not found for " + node.name() + "-" +std::to_string(node.id()));

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
        //material->setTransparency( osg::Material::FRONT_AND_BACK, 0);
        material->setEmission(osg::Material::FRONT, osg::Vec4(0.8, 0.8, 0.8, 0.5));
        // Assign the material and texture to the plane
        osg::StateSet *sphereStateSet = geode->getOrCreateStateSet();
        sphereStateSet->ref();
        sphereStateSet->setAttribute(material);
        sphereStateSet->setTextureMode(0, GL_TEXTURE_GEN_R, osg::StateAttribute::ON);
        sphereStateSet->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
    }
}

void  DSRtoOSGViewer::add_or_assign_mesh(const Node &node, const Node& parent)
{   
    qDebug() << __FUNCTION__ << "node " << node.id() << parent.id();
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
    
    osg::ref_ptr<osg::MatrixTransform> scale_transform = new osg::MatrixTransform; 			
	scale_transform->setMatrix(osg::Matrix::scale(scalex.value()/1000, scaley.value()/100, scalez.value()/10));
	osg::ref_ptr<osg::MatrixTransform> mt = new osg::MatrixTransform;
    osg::ref_ptr<osg::Node> osgMesh = osgDB::readNodeFile(filename.value());
    if (!osgMesh)
        throw  std::runtime_error("Could not find nesh file " + filename.value());
    osg::ref_ptr<osg::PolygonMode> polygonMode = new osg::PolygonMode();
    polygonMode->setMode(osg::PolygonMode::FRONT_AND_BACK, osg::PolygonMode::FILL);
    osgMesh->getOrCreateStateSet()->setAttributeAndModes(polygonMode, osg::StateAttribute::OVERRIDE | osg::StateAttribute::ON);
    osgMesh->getOrCreateStateSet()->setMode( GL_RESCALE_NORMAL, osg::StateAttribute::ON );
    scale_transform->addChild(osgMesh);
    if( auto res = osg_map.find(std::make_tuple(parent.id(), node.id())); res != osg_map.end())   
    {
        (*res).second->addChild(scale_transform);
        osg_map.insert_or_assign(std::make_tuple(node.id(), node.id()), scale_transform);
        qDebug() << __FUNCTION__ << "Added BOX, node " << node.id() << "parent "  << parent.id();
    }
    else
        throw std::runtime_error("Transform: OSG parent not found for " + node.name() + "-" +std::to_string(node.id()));

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

//////////////////////////////////////////////////////////////////////////////

void DSRtoOSGViewer::analyse_osg_graph(osg::Node *nd) 
{
	/// here you have found a group.
    osg::Geode *geode = dynamic_cast<osg::Geode *> (nd);
	if (geode) 
    { // analyse the geode. If it isnt a geode the dynamic cast gives NULL.
	    for (unsigned int i=0; i<geode->getNumDrawables(); i++) 
        {
		    osg::Drawable *drawable=geode->getDrawable(i);
		    osg::Geometry *geom=dynamic_cast<osg::Geometry *> (drawable);
		    for (unsigned int ipr=0; ipr<geom->getNumPrimitiveSets(); ipr++) 
            {
			    osg::PrimitiveSet* prset=geom->getPrimitiveSet(ipr);
			    osg::notify(osg::WARN) << "Primitive Set "<< ipr << std::endl;
			    //analysePrimSet(prset, dynamic_cast<const osg::Vec3Array*>(geom->getVertexArray()));
		    }
	    } 
    }
    else 
    {
		osg::Group *gp = dynamic_cast<osg::Group *> (nd);
		if (gp) 
        {
			osg::notify(osg::WARN) << "Group "<<  gp->getName() <<std::endl;
			for (unsigned int ic=0; ic<gp->getNumChildren(); ic++) {
				analyse_osg_graph(gp->getChild(ic));
			}
		} else 
        {
			osg::notify(osg::WARN) << "Unknown node "<<  nd <<std::endl;
		}
	}
}