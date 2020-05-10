/********************************************************************************
** Form generated from reading UI file 'graphUItabs.ui'
**
** Created by: Qt User Interface Compiler version 5.9.5
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef GRAPHUITABS_H
#define GRAPHUITABS_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_graphDlg
{
public:
    QVBoxLayout *verticalLayout_4;
    QSplitter *splitter_2;
    QSplitter *splitter_1;
    QTableWidget *tableWidgetNodes;
    QTableWidget *tableWidgetEdges;
    QTabWidget *tabWidget;
    QWidget *tab;
    QVBoxLayout *verticalLayout_2;
    QScrollArea *scrollArea;
    QWidget *scrollAreaWidgetContents;
    QVBoxLayout *verticalLayout;
    QGraphicsView *graphicsView;
    QWidget *tab_2;
    QVBoxLayout *verticalLayout_3;
    QWidget *widget;

    void setupUi(QWidget *graphDlg)
    {
        if (graphDlg->objectName().isEmpty())
            graphDlg->setObjectName(QStringLiteral("graphDlg"));
        graphDlg->resize(1028, 735);
        verticalLayout_4 = new QVBoxLayout(graphDlg);
        verticalLayout_4->setObjectName(QStringLiteral("verticalLayout_4"));
        splitter_2 = new QSplitter(graphDlg);
        splitter_2->setObjectName(QStringLiteral("splitter_2"));
        splitter_2->setOrientation(Qt::Horizontal);
        splitter_1 = new QSplitter(splitter_2);
        splitter_1->setObjectName(QStringLiteral("splitter_1"));
        splitter_1->setOrientation(Qt::Vertical);
        tableWidgetNodes = new QTableWidget(splitter_1);
        tableWidgetNodes->setObjectName(QStringLiteral("tableWidgetNodes"));
        QSizePolicy sizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(tableWidgetNodes->sizePolicy().hasHeightForWidth());
        tableWidgetNodes->setSizePolicy(sizePolicy);
        splitter_1->addWidget(tableWidgetNodes);
        tableWidgetEdges = new QTableWidget(splitter_1);
        tableWidgetEdges->setObjectName(QStringLiteral("tableWidgetEdges"));
        sizePolicy.setHeightForWidth(tableWidgetEdges->sizePolicy().hasHeightForWidth());
        tableWidgetEdges->setSizePolicy(sizePolicy);
        splitter_1->addWidget(tableWidgetEdges);
        splitter_2->addWidget(splitter_1);
        tabWidget = new QTabWidget(splitter_2);
        tabWidget->setObjectName(QStringLiteral("tabWidget"));
        tab = new QWidget();
        tab->setObjectName(QStringLiteral("tab"));
        verticalLayout_2 = new QVBoxLayout(tab);
        verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
        scrollArea = new QScrollArea(tab);
        scrollArea->setObjectName(QStringLiteral("scrollArea"));
        QSizePolicy sizePolicy1(QSizePolicy::Maximum, QSizePolicy::Expanding);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(scrollArea->sizePolicy().hasHeightForWidth());
        scrollArea->setSizePolicy(sizePolicy1);
        scrollArea->setMinimumSize(QSize(500, 0));
        scrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName(QStringLiteral("scrollAreaWidgetContents"));
        scrollAreaWidgetContents->setGeometry(QRect(0, 0, 498, 664));
        verticalLayout = new QVBoxLayout(scrollAreaWidgetContents);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        graphicsView = new QGraphicsView(scrollAreaWidgetContents);
        graphicsView->setObjectName(QStringLiteral("graphicsView"));

        verticalLayout->addWidget(graphicsView);

        scrollArea->setWidget(scrollAreaWidgetContents);

        verticalLayout_2->addWidget(scrollArea);

        tabWidget->addTab(tab, QString());
        tab_2 = new QWidget();
        tab_2->setObjectName(QStringLiteral("tab_2"));
        verticalLayout_3 = new QVBoxLayout(tab_2);
        verticalLayout_3->setObjectName(QStringLiteral("verticalLayout_3"));
        widget = new QWidget(tab_2);
        widget->setObjectName(QStringLiteral("widget"));

        verticalLayout_3->addWidget(widget);

        tabWidget->addTab(tab_2, QString());
        splitter_2->addWidget(tabWidget);

        verticalLayout_4->addWidget(splitter_2);


        retranslateUi(graphDlg);

        tabWidget->setCurrentIndex(1);


        QMetaObject::connectSlotsByName(graphDlg);
    } // setupUi

    void retranslateUi(QWidget *graphDlg)
    {
        graphDlg->setWindowTitle(QApplication::translate("graphDlg", "DSRGraph", Q_NULLPTR));
        tabWidget->setTabText(tabWidget->indexOf(tab), QApplication::translate("graphDlg", "graph", Q_NULLPTR));
        tabWidget->setTabText(tabWidget->indexOf(tab_2), QApplication::translate("graphDlg", "3d", Q_NULLPTR));
    } // retranslateUi

};

namespace Ui {
    class graphDlg: public Ui_graphDlg {};
} // namespace Ui

QT_END_NAMESPACE

#endif // GRAPHUITABS_H
