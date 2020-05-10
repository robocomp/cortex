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
    QVBoxLayout *verticalLayout_2;
    QSplitter *splittee_2;
    QSplitter *splitter_1;
    QTableWidget *tableWidgetNodes;
    QTableWidget *tableWidgetEdges;
    QScrollArea *scrollArea;
    QWidget *scrollAreaWidgetContents;
    QVBoxLayout *verticalLayout;
    QGraphicsView *graphicsView;

    void setupUi(QWidget *graphDlg)
    {
        if (graphDlg->objectName().isEmpty())
            graphDlg->setObjectName(QStringLiteral("graphDlg"));
        graphDlg->resize(784, 467);
        verticalLayout_2 = new QVBoxLayout(graphDlg);
        verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
        splittee_2 = new QSplitter(graphDlg);
        splittee_2->setObjectName(QStringLiteral("splittee_2"));
        splittee_2->setOrientation(Qt::Horizontal);
        splitter_1 = new QSplitter(splittee_2);
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
        splittee_2->addWidget(splitter_1);
        scrollArea = new QScrollArea(splittee_2);
        scrollArea->setObjectName(QStringLiteral("scrollArea"));
        scrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName(QStringLiteral("scrollAreaWidgetContents"));
        scrollAreaWidgetContents->setGeometry(QRect(0, 0, 392, 447));
        verticalLayout = new QVBoxLayout(scrollAreaWidgetContents);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        graphicsView = new QGraphicsView(scrollAreaWidgetContents);
        graphicsView->setObjectName(QStringLiteral("graphicsView"));

        verticalLayout->addWidget(graphicsView);

        scrollArea->setWidget(scrollAreaWidgetContents);
        splittee_2->addWidget(scrollArea);

        verticalLayout_2->addWidget(splittee_2);


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
