#include "dsr_to_osg_viewer.h"
#include <osg/Camera>
#include <osg/ShapeDrawable>
#include <osg/StateSet>
#include <osg/Material>
#include <osgGA/EventQueue>
#include <osgGA/TrackballManipulator>
#include <QMouseEvent>
 
using namespace DSR;

DSRtoOSGViewer::DSRtoOSGViewer(std::shared_ptr<CRDT::CRDTGraph> G, float scaleX, float scaleY, QWidget *parent) : 
                        QOpenGLWidget(parent), 
                        _mGraphicsWindow(new osgViewer::GraphicsWindowEmbedded(this->x(), this->y(), this->width(), this->height())), 
                        _mViewer(new osgViewer::Viewer),
                        m_scaleX(scaleX),
                        m_scaleY(scaleY)
{
    this->resize(parent->width(), parent->height());
    osg::Camera* camera = new osg::Camera;
    camera->setViewport( 0, 0, this->width(), this->height() );
    camera->setClearColor( osg::Vec4( 0.9f, 0.9f, 1.f, 1.f ) );
    float aspectRatio = static_cast<float>( this->width()) / static_cast<float>( this->height() );
    camera->setProjectionMatrixAsPerspective( 30.f, aspectRatio, 1.f, 1000.f );
    camera->setGraphicsContext( _mGraphicsWindow );
    _mViewer->setCamera(camera);
    osgGA::TrackballManipulator* manipulator = new osgGA::TrackballManipulator;
    manipulator->setAllowThrow( false );
    this->setMouseTracking(true);
    osg::Vec3d eye(osg::Vec3(4000.,4000.,-1500.));
    osg::Vec3d center(osg::Vec3(0.,0.,-0.));
    osg::Vec3d up(osg::Vec3(0.,1.,0.));
    manipulator->setHomePosition(eye, center, up, true);
    manipulator->setByMatrix(osg::Matrixf::lookAt(eye,center,up));
    _mViewer->setCameraManipulator(manipulator);
    _mViewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
	
 	//global stateset
	osg::StateSet *globalStateSet = new osg::StateSet;
	globalStateSet->setGlobalDefaults();
	globalStateSet->setMode(GL_DEPTH_TEST, osg::StateAttribute::ON);

	// enable lighting
	globalStateSet->setMode(GL_LIGHTING, osg::StateAttribute::ON);

	//osg::Light* light = _mViewer->getLight();
	//light->setAmbient(  osg::Vec4( 0.4f,    0.4f, 0.4f,  1.f ));
	//light->setDiffuse(  osg::Vec4( 0.8f,    0.8f, 0.8f,  1.f ));
	//light->setSpecular( osg::Vec4( 0.2f,    0.2f, 0.2f,  1.f ));
	//light->setPosition( osg::Vec4( 0.0f, 3000.0f, 0.0f,  1.f));
    // 	light->setDirection(osg::Vec3(0.0f, -1.0f, 0.0f));
	//osg::ref_ptr<osg::LightSource> lightSource = new osg::LightSource;
	//lightSource->setLight(light);
	// lightSource->setLocalStateSetModes(osg::StateAttribute::ON);
	// lightSource->setStateSetModes(*globalStateSet,osg::StateAttribute::ON);
	//root->addChild(lightSource.get() );

    root = new osg::Group();
    add_cylinder();
	_mViewer->setSceneData( root.get());
    
    // connect(G.get(), &CRDT::CRDTGraph::update_node_signal, this, &GraphViewer::addOrAssignNodeSLOT);
	// connect(G.get(), &CRDT::CRDTGraph::update_edge_signal, this, &GraphViewer::addEdgeSLOT);
	// connect(G.get(), &CRDT::CRDTGraph::del_edge_signal, this, &GraphViewer::delEdgeSLOT);
	// connect(G.get(), &CRDT::CRDTGraph::del_node_signal, this, &GraphViewer::delNodeSLOT);

     _mViewer->realize();
}

void DSRtoOSGViewer::add_cylinder()
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
    return eventQueue;
}