/********************************************************************************
** Form generated from reading UI file 'graphUI.ui'
**
** Created by: Qt User Interface Compiler version 5.9.5
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef GRAPHUI_H
#define GRAPHUI_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_graphDlg
{
public:
    QVBoxLayout *verticalLayout;
    QSplitter *splitter_2;
    QSplitter *splitter_1;
    QTableWidget *tableWidgetNodes;
    QTableWidget *tableWidgetEdges;
    QScrollArea *scrollArea;
    QWidget *scrollAreaWidgetContents;
    QHBoxLayout *horizontalLayout;
    QGraphicsView *graphicsView;

    void setupUi(QWidget *graphDlg)
    {
        if (graphDlg->objectName().isEmpty())
            graphDlg->setObjectName(QStringLiteral("graphDlg"));
        graphDlg->resize(784, 445);
        verticalLayout = new QVBoxLayout(graphDlg);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
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
        scrollArea = new QScrollArea(splitter_2);
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
        scrollAreaWidgetContents->setGeometry(QRect(0, 0, 501, 425));
        horizontalLayout = new QHBoxLayout(scrollAreaWidgetContents);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        graphicsView = new QGraphicsView(scrollAreaWidgetContents);
        graphicsView->setObjectName(QStringLiteral("graphicsView"));

        horizontalLayout->addWidget(graphicsView);

        scrollArea->setWidget(scrollAreaWidgetContents);
        splitter_2->addWidget(scrollArea);

        verticalLayout->addWidget(splitter_2);


        retranslateUi(graphDlg);

        QMetaObject::connectSlotsByName(graphDlg);
    } // setupUi

    void retranslateUi(QWidget *graphDlg)
    {
        graphDlg->setWindowTitle(QApplication::translate("graphDlg", "DSRGraph", Q_NULLPTR));
    } // retranslateUi

};

namespace Ui {
    class graphDlg: public Ui_graphDlg {};
} // namespace Ui

QT_END_NAMESPACE

#endif // GRAPHUI_H
