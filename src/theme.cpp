#include "theme.h"

namespace Theme {

QString getDarkTheme() {
    return R"QSS(
        /* Global Styles */
        QWidget {
            color: #a9b1d6;
            font-family: 'Inter', 'Segoe UI', 'Ubuntu', sans-serif;
            font-size: 13px;
        }

        /* MainWindow and dialog containers */
        QMainWindow, QDialog, QFrame#sidebarContainer, QTabWidget::pane {
            background-color: #1a1b26;
        }
        #headerWidget {
            background-color: #16161e;
            border-bottom: 1px solid #24283b;
        }
        #verticalTabStrip {
            background-color: #101014;
            border-right: 1px solid #24283b;
        }
        #multiInputBar {
            background-color: #16161e;
            border-top: 1px solid #24283b;
        }

        /* Sidebar Container */
        QFrame#sidebarContainer {
            background-color: #16161e;
            border-right: 1px solid #24283b;
            border-radius: 0px;
        }

        /* Sidebar Tabs / Buttons */
        QToolButton {
            background-color: transparent;
            border: none;
            border-radius: 6px;
            color: #787c99;
            padding: 8px;
            margin: 4px 6px;
            font-weight: bold;
        }
        QToolButton:hover {
            background-color: rgba(255, 255, 255, 0.05);
            color: #c0caf5;
        }
        QToolButton:checked {
            background-color: rgba(65, 166, 250, 0.15);
            color: #7aa2f7;
            border-left: 3px solid #7aa2f7;
            border-radius: 0px 6px 6px 0px;
        }

        /* Tab Widget and Tab Bar */
        QTabWidget::pane {
            border: 1px solid #24283b;
            background-color: #1a1b26;
            border-radius: 6px;
            top: -1px;
        }
        QTabBar::tab {
            background-color: #16161e;
            color: #787c99;
            padding: 8px 16px;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            border: 1px solid #24283b;
            border-bottom: none;
            margin-right: 2px;
        }
        QTabBar::tab:hover {
            background-color: #1e1e2e;
            color: #c0caf5;
        }
        QTabBar::tab:selected {
            background-color: #1a1b26;
            color: #7aa2f7;
            border-bottom: 2px solid #7aa2f7;
            font-weight: bold;
        }
        QTabBar::close-button {
            image: url(:/icons/close.svg);
            subcontrol-position: right;
            width: 12px;
            height: 12px;
            border-radius: 3px;
        }
        QTabBar::close-button:hover {
            background-color: rgba(255, 255, 255, 0.15);
        }

        /* Buttons */
        QPushButton {
            background-color: #24283b;
            color: #c0caf5;
            border: 1px solid #414868;
            border-radius: 6px;
            padding: 6px 12px;
            min-height: 20px;
        }
        QDialogButtonBox QPushButton {
            qproperty-icon: url();
        }
        QPushButton:hover {
            background-color: #414868;
            border: 1px solid #7aa2f7;
        }
        QPushButton:pressed {
            background-color: #1f2335;
        }
        QPushButton#primaryButton {
            background-color: #7aa2f7;
            color: #1a1b26;
            border: 1px solid #7aa2f7;
            font-weight: bold;
        }
        QPushButton#primaryButton:hover {
            background-color: #89ddff;
            border: 1px solid #89ddff;
        }

        /* Inputs, Combo Boxes, Spinboxes */
        QLineEdit, QComboBox, QSpinBox {
            background-color: #16161e;
            border: 1px solid #24283b;
            border-radius: 6px;
            padding: 6px;
            color: #c0caf5;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
            border: 1px solid #7aa2f7;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 24px;
            border-left: none;
            background: transparent;
        }
        QComboBox::down-arrow {
            image: url(:/icons/chevron-down.svg);
            width: 12px;
            height: 12px;
        }

        /* List and Tree Views */
        QListView, QTreeView {
            background-color: #16161e;
            border: 1px solid #24283b;
            border-radius: 6px;
            show-decoration-selected: 1;
            padding: 4px;
        }
        QTreeView::branch:has-children:closed:has-members {
            image: url(:/icons/chevron-right.svg);
        }
        QTreeView::branch:has-children:open:has-members {
            image: url(:/icons/chevron-down.svg);
        }
        QListView::item, QTreeView::item {
            padding: 6px;
            border-radius: 4px;
            margin: 1px 0px;
        }
        QListView::item:hover, QTreeView::item:hover {
            background-color: rgba(255, 255, 255, 0.03);
            color: #c0caf5;
        }
        QListView::item:selected, QTreeView::item:selected {
            background-color: rgba(122, 162, 247, 0.2);
            color: #7aa2f7;
            font-weight: bold;
        }

        /* Splitter */
        QSplitter::handle {
            background-color: #24283b;
        }
        QSplitter::handle:horizontal {
            width: 4px;
        }
        QSplitter::handle:vertical {
            height: 4px;
        }

        /* Labels and Headers */
        QLabel {
            color: #a9b1d6;
        }
        QHeaderView::section {
            background-color: #16161e;
            color: #787c99;
            padding: 6px;
            border: none;
            border-bottom: 1px solid #24283b;
            font-weight: bold;
        }

        /* Scrollbars */
        QScrollBar:vertical {
            border: none;
            background: #16161e;
            width: 10px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #24283b;
            min-height: 20px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical:hover {
            background: #414868;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar:horizontal {
            border: none;
            background: #16161e;
            height: 10px;
            margin: 0px;
        }
        QScrollBar::handle:horizontal {
            background: #24283b;
            min-width: 20px;
            border-radius: 5px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #414868;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }
    )QSS";
}

QString getLightTheme() {
    return R"QSS(
        /* Global Styles */
        QWidget {
            color: #333333;
            font-family: 'Inter', 'Segoe UI', 'Ubuntu', sans-serif;
            font-size: 13px;
        }

        /* MainWindow and dialog containers */
        QMainWindow, QDialog, QFrame#sidebarContainer, QTabWidget::pane {
            background-color: #f5f6f9;
        }
        #headerWidget {
            background-color: #eaedf3;
            border-bottom: 1px solid #d0d7de;
        }
        #verticalTabStrip {
            background-color: #e4e7eb;
            border-right: 1px solid #d0d7de;
        }
        #multiInputBar {
            background-color: #eaedf3;
            border-top: 1px solid #d0d7de;
        }

        /* Sidebar Container */
        QFrame#sidebarContainer {
            background-color: #eaedf3;
            border-right: 1px solid #d0d7de;
            border-radius: 0px;
        }

        /* Sidebar Tabs / Buttons */
        QToolButton {
            background-color: transparent;
            border: none;
            border-radius: 6px;
            color: #555555;
            padding: 8px;
            margin: 4px 6px;
            font-weight: bold;
        }
        QToolButton:hover {
            background-color: rgba(0, 0, 0, 0.05);
            color: #000000;
        }
        QToolButton:checked {
            background-color: rgba(30, 144, 255, 0.15);
            color: #1e90ff;
            border-left: 3px solid #1e90ff;
            border-radius: 0px 6px 6px 0px;
        }

        /* Tab Widget and Tab Bar */
        QTabWidget::pane {
            border: 1px solid #d0d7de;
            background-color: #ffffff;
            border-radius: 6px;
            top: -1px;
        }
        QTabBar::tab {
            background-color: #eaedf3;
            color: #555555;
            padding: 8px 16px;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            border: 1px solid #d0d7de;
            border-bottom: none;
            margin-right: 2px;
        }
        QTabBar::tab:hover {
            background-color: #f0f3f6;
            color: #000000;
        }
        QTabBar::tab:selected {
            background-color: #ffffff;
            color: #1e90ff;
            border-bottom: 2px solid #1e90ff;
            font-weight: bold;
        }
        QTabBar::close-button {
            image: url(:/icons/close.svg);
            subcontrol-position: right;
            width: 12px;
            height: 12px;
            border-radius: 3px;
        }
        QTabBar::close-button:hover {
            background-color: rgba(0, 0, 0, 0.1);
        }

        /* Buttons */
        QPushButton {
            background-color: #e4e7eb;
            color: #333333;
            border: 1px solid #ccc;
            border-radius: 6px;
            padding: 6px 12px;
            min-height: 20px;
        }
        QDialogButtonBox QPushButton {
            qproperty-icon: url();
        }
        QPushButton:hover {
            background-color: #d8dbe0;
            border: 1px solid #1e90ff;
        }
        QPushButton:pressed {
            background-color: #cbd0d6;
        }
        QPushButton#primaryButton {
            background-color: #1e90ff;
            color: #ffffff;
            border: 1px solid #1e90ff;
            font-weight: bold;
        }
        QPushButton#primaryButton:hover {
            background-color: #007aff;
            border: 1px solid #007aff;
        }

        /* Inputs, Combo Boxes, Spinboxes */
        QLineEdit, QComboBox, QSpinBox {
            background-color: #ffffff;
            border: 1px solid #ccc;
            border-radius: 6px;
            padding: 6px;
            color: #333333;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
            border: 1px solid #1e90ff;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 24px;
            border-left: none;
            background: transparent;
        }
        QComboBox::down-arrow {
            image: url(:/icons/chevron-down.svg);
            width: 12px;
            height: 12px;
        }

        /* List and Tree Views */
        QListView, QTreeView {
            background-color: #ffffff;
            border: 1px solid #ccc;
            border-radius: 6px;
            show-decoration-selected: 1;
            padding: 4px;
        }
        QTreeView::branch:has-children:closed:has-members {
            image: url(:/icons/chevron-right.svg);
        }
        QTreeView::branch:has-children:open:has-members {
            image: url(:/icons/chevron-down.svg);
        }
        QListView::item, QTreeView::item {
            padding: 6px;
            border-radius: 4px;
            margin: 1px 0px;
        }
        QListView::item:hover, QTreeView::item:hover {
            background-color: rgba(0, 0, 0, 0.03);
            color: #000000;
        }
        QListView::item:selected, QTreeView::item:selected {
            background-color: rgba(30, 144, 255, 0.15);
            color: #1e90ff;
            font-weight: bold;
        }

        /* Splitter */
        QSplitter::handle {
            background-color: #d0d7de;
        }
        QSplitter::handle:horizontal {
            width: 4px;
        }
        QSplitter::handle:vertical {
            height: 4px;
        }

        /* Labels and Headers */
        QLabel {
            color: #333333;
        }
        QHeaderView::section {
            background-color: #eaedf3;
            color: #555555;
            padding: 6px;
            border: none;
            border-bottom: 1px solid #d0d7de;
            font-weight: bold;
        }

        /* Scrollbars */
        QScrollBar:vertical {
            border: none;
            background: #f5f6f9;
            width: 10px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #ccc;
            min-height: 20px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical:hover {
            background: #bbb;
        }
        QScrollBar:horizontal {
            border: none;
            background: #f5f6f9;
            height: 10px;
            margin: 0px;
        }
        QScrollBar::handle:horizontal {
            background: #ccc;
            min-width: 20px;
            border-radius: 5px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #bbb;
        }
    )QSS";
}

} // namespace Theme
