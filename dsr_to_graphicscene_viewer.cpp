#include "dsr_to_graphicscene_viewer.h"

using namespace DSR ;

DSRtoGraphicsceneViewer::DSRtoGraphicsceneViewer(std::shared_ptr<CRDT::CRDTGraph> G_, float scaleX, float scaleY, QGraphicsView *parent) : QGraphicsView(parent)
{
    G = G_;
    m_scaleX = scaleX;
    m_scaleY = scaleY;

    this->resize(parent->width(), parent->height());
    //this->setFrameShape(NoFrame);
    scene.setItemIndexMethod(QGraphicsScene::NoIndex);
    scene.setSceneRect(-5000, -5000, 10000, 10000);
	this->scale(1, -1);
    this->setScene(&scene);
    this->setCacheMode(QGraphicsView::CacheBackground);
	this->setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
	this->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
	this->setRenderHint(QPainter::Antialiasing);
	this->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	this->fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
	
 	setMouseTracking(true);
    this->viewport()->setMouseTracking(true);

    //center position
    scene.addRect(-100, -100, 200, 200, QPen(QColor("black")),QBrush(QColor("black")));
    //edge => x red, z (y) blue
    scene.addRect(-3000, -3000, 1000, 30, QPen(QColor("red")),QBrush(QColor("red")));
    scene.addRect(-3000, -3000, 30, 1000, QPen(QColor("blue")),QBrush(QColor("blue")));


    createGraph();
}

void DSRtoGraphicsceneViewer::createGraph()
{
    innermodel = G->get_inner_api();
    try
    {
        auto map = G->getCopy();
		for(const auto &[k, node] : map)
		       add_or_assign_node_slot(k,  node.type());
/*		for(auto node : map) 
           	for(const auto &[k, edges] : node.second.fano())
			    add_or_assign_edge_slot(edges.from(), edges.to(), edges.type());
*/    }
	catch(const std::exception &e) { std::cout << e.what() << " Error accessing "<< __FUNCTION__<<":"<<__LINE__<< std::endl;}
    
}

//////////////////////////////////////////////////////////////////////////////////////
///// SLOTS
//////////////////////////////////////////////////////////////////////////////////////

void DSRtoGraphicsceneViewer::add_or_assign_node_slot(const std::int32_t id, const std::string &type)
{
    qDebug() << __FUNCTION__ ;
    qDebug()<<"*************************";
    
    auto node = G->get_node(id);
    std::cout << node.value().name() << " " << node.value().id() << std::endl;
    if(node.has_value())
    {
    if( type == "plane" )//or type == "floor")
        add_or_assign_box(node.value());
    if( type == "mesh")
        add_or_assign_mesh(node.value());
    }
}
void DSRtoGraphicsceneViewer::add_or_assign_edge_slot(const std::int32_t from, const std::int32_t to, const std::string& type)
{
    qDebug() << __FUNCTION__ ;
}

void DSRtoGraphicsceneViewer::add_or_assign_box(Node &node)
{
    qDebug() << "********************************";
    qDebug() << __FUNCTION__ ;
    std::string color = G->get_attrib_by_name<std::string>(node, "color").value_or("orange");
    std::string filename = G->get_attrib_by_name<std::string>(node, "texture").value_or("");
    auto width = G->get_attrib_by_name<std::int32_t>(node, "width");
    auto height = G->get_attrib_by_name<std::int32_t>(node, "height");


    if(width.has_value()) std::cout << "width:" << width.value() << std::endl;    
    if(height.has_value()) std::cout << "height: " << height.value() << std::endl;


    //check if has required values
    if(width.has_value() and height.has_value())
    {
        add_or_assign_object(width.value(), height.value(), node.name(), color, filename); 
    }
    else
    {
        std::cout<<"Error drawing "<< node << " width or height required attribs has no value"<<std::endl;
    }
    

}

void  DSRtoGraphicsceneViewer::add_or_assign_mesh(Node &node)
{   
    qDebug() << "********************************";
    qDebug() << __FUNCTION__ ;
    std::string color = G->get_attrib_by_name<std::string>(node, "color").value_or("orange");
    std::string filename = G->get_attrib_by_name<std::string>(node, "path").value_or("");
    auto scalex = G->get_attrib_by_name<std::int32_t>(node, "scalex");
    auto scaley = G->get_attrib_by_name<std::int32_t>(node, "scaley");
    auto scalez = G->get_attrib_by_name<std::int32_t>(node, "scalez");

    if(scalex.has_value()) std::cout << scalex.value() << std::endl;
    if(scaley.has_value()) std::cout << scaley.value() << std::endl;
    if(scalez.has_value()) std::cout << scalez.value() << std::endl;

     //check if has required values
    if(scalex.has_value() and scaley.has_value() and scalez.has_value())
    {
        add_or_assign_object(scalex.value(), scalez.value(), node.name(), color, filename);
    }
    else
    {
        std::cout<<"Error drawing "<< node << " scalex, scaley or scalez required attribs has no value"<<std::endl;
    }
}

void  DSRtoGraphicsceneViewer::add_or_assign_object(int width, int height, std::string node_name, std::string color, std::string filename)
{
    // get transfrom to world => to get correct position
    std::optional<QVec> pose = innermodel->transformS6D("world", node_name);
    if (pose.has_value())
    {
pose.value().print(QString::fromStdString(node_name));

        add_scene_rect(width, height, pose.value(), color, filename);
qDebug()<<"Node"<<QString::fromStdString(node_name)<<"zvalue"<<pose.value().y();
    }
    else
    {
        qDebug()<<"Error gettion tranformation from node"<<QString::fromStdString(node_name)<<"to world";
    }
    
}

void DSRtoGraphicsceneViewer::add_scene_rect(int width, int height, QVec pose, std::string color, std::string texture)
{
    std::cout<<"color"<<color;
    QBrush brush = QBrush(QColor(QString::fromStdString(color)));
    if (texture != "")
    {
        if(std::filesystem::exists(texture))
            brush = QBrush(QImage(QString::fromStdString(texture)));
        else
            brush = QBrush(QColor(QString::fromStdString(texture)));
    }

    QGraphicsRectItem *box = scene.addRect(pose.x()-width/2 , pose.z() - height/2, width, height, QPen(QString::fromStdString(color)), brush);
    box->setRotation(pose.ry()*180/M_PI);
    box->setZValue(pose.y());
}






//////////////////////////////////////////////////////////////
//                  MOUSE                                   //
//////////////////////////////////////////////////////////////
void DSRtoGraphicsceneViewer::wheelEvent(QWheelEvent* event)
{
//    qDebug()<<"wheel";
    const QGraphicsView::ViewportAnchor anchor = this->transformationAnchor();
	this->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	qreal factor;
	if (event->angleDelta().y() > 0) 
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
	this->scale(factor, factor);
	this->setTransformationAnchor(anchor);
}

void DSRtoGraphicsceneViewer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton)
    {
        _pan = true;
        _panStartX = event->x();
        _panStartY = event->y();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    event->ignore();
}

void DSRtoGraphicsceneViewer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton)
    {
        _pan = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    event->ignore();
}

void DSRtoGraphicsceneViewer::mouseMoveEvent(QMouseEvent *event)
{
    if (_pan)
    {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - (event->x() - _panStartX));
        verticalScrollBar()->setValue(verticalScrollBar()->value() - (event->y() - _panStartY));
        _panStartX = event->x();
        _panStartY = event->y();
        event->accept();
        return;
    }
    event->ignore();
}