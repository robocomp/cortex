/*
 * Copyright 2018 <copyright holder> <email>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GRAPHVIEWER_H
#define GRAPHVIEWER_H

#include <memory>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QListView>
#include "CRDT.h"
#include <typeinfo>
#include "ui_graphUI.h"
#include <QResizeEvent>
#include <QOpenGLWidget>
#include "../../../graph-related-classes/dsr_to_osg_viewer.h"
#include "../../../graph-related-classes/dsr_to_graphicscene_viewer.h"
#include "../../../graph-related-classes/dsr_to_graph_viewer.h"

namespace DSR
{
	//////////////////////////////////////////////////////////////////////////////////////////////77
	/// Drawing controller to display the graph in real-time using RTPS 
	//////////////////////////////////////////////////////////////////////////////////////////////77
	
	//class GraphViewer : public QGraphicsView 
	class GraphViewer : public QWidget, public Ui_graphDlg
	{
		Q_OBJECT
		public:
			GraphViewer(std::shared_ptr<CRDT::CRDTGraph> G);
			~GraphViewer();
			void itemMoved();
			void createGraph();
			
		protected:
			virtual void keyPressEvent(QKeyEvent *event);
			virtual void timerEvent(QTimerEvent *event);
			// Resize tabs. The event does not propagate for some reason
			virtual void resizeEvent(QResizeEvent *e) 
			{  
				dsr_to_graph_viewer->resize(graphicsView->size());
				dsr_to_graph_viewer->fitInView(dsr_to_graph_viewer->scene.sceneRect(), Qt::KeepAspectRatio );
			 	auto gl =tab_2->findChildren<QOpenGLWidget*>();
			 	if(gl.size()>0)
			 		gl[0]->resize(e->size().width(), e->size().height());
				gl =tab_3->findChildren<QOpenGLWidget*>();
			 	if(gl.size()>0)
			 		gl[0]->resize(e->size().width(), e->size().height());
			 };
			
		private:
			std::shared_ptr<CRDT::CRDTGraph> G;
			int timerId = 0;
			bool do_simulate = false;
			std::unique_ptr<DSR::DSRtoOSGViewer> dsr_to_osg_viewer;
			std::unique_ptr<DSR::DSRtoGraphicsceneViewer> dsr_to_graphicscene_viewer;
			std::unique_ptr<DSR::DSRtoGraphViewer> dsr_to_graph_viewer;

		public slots:
			void saveGraphSLOT();		
			void toggleSimulationSLOT();

		signals:
			void saveGraphSIGNAL();
			void closeWindowSIGNAL();
	};
}

Q_DECLARE_METATYPE(std::int32_t);
Q_DECLARE_METATYPE(std::string);
Q_DECLARE_METATYPE(CRDT::AttribsMap);

#endif // GRAPHVIEWER_H
