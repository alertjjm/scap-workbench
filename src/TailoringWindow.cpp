/*
 * Copyright 2013 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *      Martin Preisler <mpreisle@redhat.com>
 */

#include "TailoringWindow.h"
#include "Exceptions.h"
#include "MainWindow.h"
#include "APIHelpers.h"
#include "Utils.h"

#include <QMessageBox>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QUndoView>

#include <QDebug>

#include <set>
#include <algorithm>
#include <cassert>

ProfilePropertiesDockWidget::ProfilePropertiesDockWidget(TailoringWindow* window, QWidget* parent):
    QDockWidget(parent),

    mRefreshInProgress(false),
    mWindow(window)
{
    mUI.setupUi(this);

    QObject::connect(
        mUI.title, SIGNAL(textChanged(const QString&)),
        this, SLOT(profileTitleChanged(const QString&))
    );

    QObject::connect(
        mUI.description, SIGNAL(textChanged()),
        this, SLOT(profileDescriptionChanged())
    );
}

ProfilePropertiesDockWidget::~ProfilePropertiesDockWidget()
{}

void ProfilePropertiesDockWidget::refresh()
{
    if (mUI.id->text() != mWindow->getProfileID())
        mUI.id->setText(mWindow->getProfileID());

    if (mUI.title->text() != mWindow->getProfileTitle())
    {
        // This prevents a new undo command being spawned as a result of refreshing
        mRefreshInProgress = true;
        mUI.title->setText(mWindow->getProfileTitle());
        mRefreshInProgress = false;
    }

    if (mUI.description->toPlainText() != mWindow->getProfileDescription())
    {
        // This prevents a new undo command being spawned as a result of refreshing
        mRefreshInProgress = true;
        mUI.description->setPlainText(mWindow->getProfileDescription());
        mRefreshInProgress = false;
    }
}

void ProfilePropertiesDockWidget::profileTitleChanged(const QString& newTitle)
{
    if (mRefreshInProgress)
        return;

    mWindow->setProfileTitleWithUndoCommand(newTitle);
}

void ProfilePropertiesDockWidget::profileDescriptionChanged()
{
    if (mRefreshInProgress)
        return;

    mWindow->setProfileDescriptionWithUndoCommand(mUI.description->toPlainText());
}

XCCDFItemPropertiesDockWidget::XCCDFItemPropertiesDockWidget(TailoringWindow* window, QWidget* parent):
    QDockWidget(parent),

    mXccdfItem(0),
    mXccdfPolicy(0),

    mRefreshInProgress(false),

    mWindow(window)
{
    mUI.setupUi(this);

    QObject::connect(
        mUI.valueComboBox, SIGNAL(editTextChanged(const QString&)),
        this, SLOT(valueChanged(const QString&))
    );
}

XCCDFItemPropertiesDockWidget::~XCCDFItemPropertiesDockWidget()
{}

void XCCDFItemPropertiesDockWidget::setXccdfItem(struct xccdf_item* item, struct xccdf_policy* policy)
{
    mXccdfItem = item;
    mXccdfPolicy = policy;

    refresh();
}

void XCCDFItemPropertiesDockWidget::refresh()
{
    if (mRefreshInProgress)
        return;

    mRefreshInProgress = true;

    mUI.titleLineEdit->setText("<no item selected>");
    mUI.idLineEdit->setText("");
    mUI.typeLineEdit->setText("");
    mUI.descriptionBrowser->setHtml("");

    mUI.valueGroupBox->hide();
    mUI.valueComboBox->clear();
    mUI.valueComboBox->setEditText("");
    mUI.valueComboBox->lineEdit()->setValidator(0);

    if (mXccdfItem)
    {
        mUI.titleLineEdit->setText(oscapTextIteratorGetPreferred(xccdf_item_get_title(mXccdfItem)));
        mUI.idLineEdit->setText(QString::fromUtf8(xccdf_item_get_id(mXccdfItem)));
        switch (xccdf_item_get_type(mXccdfItem))
        {
            case XCCDF_BENCHMARK:
                mUI.typeLineEdit->setText("xccdf:Benchmark");
                break;
            case XCCDF_GROUP:
                mUI.typeLineEdit->setText("xccdf:Group");
                break;
            case XCCDF_RULE:
                mUI.typeLineEdit->setText("xccdf:Rule");
                break;
            case XCCDF_VALUE:
                mUI.typeLineEdit->setText("xccdf:Value");
                break;

            default:
                break;
        }
        mUI.descriptionBrowser->setHtml(oscapTextIteratorGetPreferred(xccdf_item_get_description(mXccdfItem)));

        if (xccdf_item_get_type(mXccdfItem) == XCCDF_VALUE)
        {
            struct xccdf_value* value = xccdf_item_to_value(mXccdfItem);
            xccdf_value_type_t valueType = xccdf_value_get_type(value);

            switch (valueType)
            {
                case XCCDF_TYPE_NUMBER:
                    // XCCDF specification says:
                    // if element’s @type attribute is “number”, then a tool might choose
                    // to reject user tailoring input that is not composed of digits.
                    //
                    // This implies integers and not decimals.
                    mUI.valueComboBox->lineEdit()->setValidator(new QIntValidator());
                    mUI.valueTypeLabel->setText("(number)");
                    break;
                case XCCDF_TYPE_STRING:
                    mUI.valueComboBox->lineEdit()->setValidator(0);
                    mUI.valueTypeLabel->setText("(string)");
                    break;
                case XCCDF_TYPE_BOOLEAN:
                    // This is my best effort since the specification doesn't say what should be allowed.
                    const QRegExp regex("true|false|True|False|TRUE|FALSE|1|0|yes|no|Yes|No|YES|NO");
                    mUI.valueComboBox->lineEdit()->setValidator(new QRegExpValidator(regex));
                    mUI.valueTypeLabel->setText("(bool)");
                    break;
            }

            struct xccdf_value_instance_iterator* it = xccdf_value_get_instances(value);
            while (xccdf_value_instance_iterator_has_more(it))
            {
                struct xccdf_value_instance* instance = xccdf_value_instance_iterator_next(it);
                mUI.valueComboBox->addItem(QString::fromUtf8(xccdf_value_instance_get_value(instance)));
            }
            xccdf_value_instance_iterator_free(it);

            mUI.valueComboBox->setEditText(mWindow->getCurrentValueValue(value));

            mUI.valueComboBox->insertSeparator(1);
            mUI.valueGroupBox->show();
        }
    }

    mRefreshInProgress = false;
}

void XCCDFItemPropertiesDockWidget::valueChanged(const QString& newValue)
{
    if (mRefreshInProgress)
        return;

    mWindow->setValueValueWithUndoCommand(xccdf_item_to_value(mXccdfItem), newValue);
}

inline struct xccdf_item* getXccdfItemFromTreeItem(QTreeWidgetItem* treeItem)
{
    QVariant xccdfItem = treeItem->data(0, Qt::UserRole);
    return reinterpret_cast<struct xccdf_item*>(xccdfItem.value<void*>());
}

ProfileTitleChangeUndoCommand::ProfileTitleChangeUndoCommand(TailoringWindow* window, const QString& oldTitle, const QString& newTitle):
    mWindow(window),
    mOldTitle(oldTitle),
    mNewTitle(newTitle)
{
    setText(QString("profile title to \"%1\"").arg(newTitle));
}

ProfileTitleChangeUndoCommand::~ProfileTitleChangeUndoCommand()
{}

int ProfileTitleChangeUndoCommand::id() const
{
    return 2;
}

void ProfileTitleChangeUndoCommand::redo()
{
    mWindow->setProfileTitle(mNewTitle);
    mWindow->refreshProfileDockWidget();
}

void ProfileTitleChangeUndoCommand::undo()
{
    mWindow->setProfileTitle(mOldTitle);
    mWindow->refreshProfileDockWidget();
}

bool ProfileTitleChangeUndoCommand::mergeWith(const QUndoCommand *other)
{
    if (other->id() != id())
        return false;

    mNewTitle = static_cast<const ProfileTitleChangeUndoCommand*>(other)->mNewTitle;
    return true;
}

ProfileDescriptionChangeUndoCommand::ProfileDescriptionChangeUndoCommand(TailoringWindow* window, const QString& oldDesc, const QString& newDesc):
    mWindow(window),
    mOldDesc(oldDesc),
    mNewDesc(newDesc)
{
    QString shortDesc = newDesc;
    shortDesc.truncate(32);
    shortDesc += "...";

    setText(QString("profile description to \"%1\"").arg(shortDesc));
}

ProfileDescriptionChangeUndoCommand::~ProfileDescriptionChangeUndoCommand()
{}

int ProfileDescriptionChangeUndoCommand::id() const
{
    return 3;
}

void ProfileDescriptionChangeUndoCommand::redo()
{
    mWindow->setProfileDescription(mNewDesc);
    mWindow->refreshProfileDockWidget();
}

void ProfileDescriptionChangeUndoCommand::undo()
{
    mWindow->setProfileDescription(mOldDesc);
    mWindow->refreshProfileDockWidget();
}

bool ProfileDescriptionChangeUndoCommand::mergeWith(const QUndoCommand *other)
{
    if (other->id() != id())
        return false;

    mNewDesc = static_cast<const ProfileDescriptionChangeUndoCommand*>(other)->mNewDesc;
    return true;
}

XCCDFItemSelectUndoCommand::XCCDFItemSelectUndoCommand(TailoringWindow* window, QTreeWidgetItem* item, bool newSelect):
    mWindow(window),
    mTreeItem(item),
    mNewSelect(newSelect)
{
    struct xccdf_item* xccdfItem = getXccdfItemFromTreeItem(mTreeItem);
    setText(QString(mNewSelect ? "select" : "unselect") + QString(" '%1'").arg(QString::fromUtf8(xccdf_item_get_id(xccdfItem))));
}

XCCDFItemSelectUndoCommand::~XCCDFItemSelectUndoCommand()
{}

int XCCDFItemSelectUndoCommand::id() const
{
    return 1;
}

void XCCDFItemSelectUndoCommand::redo()
{
    struct xccdf_item* xccdfItem = getXccdfItemFromTreeItem(mTreeItem);
    mWindow->setItemSelected(xccdfItem, mNewSelect);
    mWindow->synchronizeTreeItem(mTreeItem, xccdfItem, false);
}

void XCCDFItemSelectUndoCommand::undo()
{
    struct xccdf_item* xccdfItem = getXccdfItemFromTreeItem(mTreeItem);
    mWindow->setItemSelected(xccdfItem, !mNewSelect);
    mWindow->synchronizeTreeItem(mTreeItem, xccdfItem, false);
}

XCCDFValueChangeUndoCommand::XCCDFValueChangeUndoCommand(TailoringWindow* window, struct xccdf_value* xccdfValue, const QString& newValue, const QString& oldValue):
    mWindow(window),
    mXccdfValue(xccdfValue),

    mNewValue(newValue),
    mOldValue(oldValue)
{
    refreshText();
}

XCCDFValueChangeUndoCommand::~XCCDFValueChangeUndoCommand()
{}

void XCCDFValueChangeUndoCommand::refreshText()
{
    setText(QString("set value '%1' to '%2'").arg(xccdf_value_get_id(mXccdfValue)).arg(mNewValue));
}

int XCCDFValueChangeUndoCommand::id() const
{
    return 4;
}

bool XCCDFValueChangeUndoCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;

    const XCCDFValueChangeUndoCommand* command = static_cast<const XCCDFValueChangeUndoCommand*>(other);

    if (command->mXccdfValue != mXccdfValue)
        return false;

    mNewValue = command->mNewValue;
    refreshText();
    return true;
}

void XCCDFValueChangeUndoCommand::redo()
{
    mWindow->setValueValue(mXccdfValue, mNewValue);
    mWindow->refreshXccdfItemPropertiesDockWidget();
}

void XCCDFValueChangeUndoCommand::undo()
{
    mWindow->setValueValue(mXccdfValue, mOldValue);
    mWindow->refreshXccdfItemPropertiesDockWidget();
}

/**
 * This only handles changes in selection of just one tree item!
 */
void _syncXCCDFItemChildrenDisabledState(QTreeWidgetItem* treeItem, bool enabled)
{
    for (int i = 0; i < treeItem->childCount(); ++i)
    {
        QTreeWidgetItem* childTreeItem = treeItem->child(i);
        const bool childEnabled = !childTreeItem->isDisabled();

        if (!enabled && childEnabled)
        {
            childTreeItem->setDisabled(true);
            _syncXCCDFItemChildrenDisabledState(childTreeItem, false);
        }
        else if (enabled && !childEnabled)
        {
            childTreeItem->setDisabled(false);
            _syncXCCDFItemChildrenDisabledState(childTreeItem, true);
        }
    }
}

void _refreshXCCDFItemChildrenDisabledState(QTreeWidgetItem* treeItem, bool allAncestorsSelected)
{
    bool itemSelected = !(treeItem->flags() & Qt::ItemIsUserCheckable) || treeItem->checkState(0) == Qt::Checked;
    allAncestorsSelected = allAncestorsSelected && itemSelected;

    for (int i = 0; i < treeItem->childCount(); ++i)
    {
        QTreeWidgetItem* childTreeItem = treeItem->child(i);
        childTreeItem->setDisabled(!allAncestorsSelected);

        _refreshXCCDFItemChildrenDisabledState(childTreeItem, allAncestorsSelected);
    }
}

TailoringWindow::TailoringWindow(struct xccdf_policy* policy, struct xccdf_benchmark* benchmark, bool newProfile, MainWindow* parent):
    QMainWindow(parent),

    mParentMainWindow(parent),

    mSynchronizeItemLock(0),

    mItemPropertiesDockWidget(new XCCDFItemPropertiesDockWidget(this)),
    mProfilePropertiesDockWidget(new ProfilePropertiesDockWidget(this, this)),
    mUndoViewDockWidget(new QDockWidget(this)),

    mPolicy(policy),
    mProfile(xccdf_policy_get_profile(policy)),
    mBenchmark(benchmark),

    mUndoStack(this),

    mNewProfile(newProfile),
    mChangesConfirmed(false)
{
    // sanity check
    if (!mPolicy)
        throw TailoringWindowException("TailoringWindow needs a proper policy "
            "being given. NULL was given instead!");

    if (!mProfile)
        throw TailoringWindowException("TailoringWindow was given a non-NULL "
            "policy but profile associated with it is NULL. Can't proceed!");

    if (!mBenchmark)
        throw TailoringWindowException("TailoringWindow was given a NULL "
            "benchmark. Can't proceed!");

    mUI.setupUi(this);

    QObject::connect(
        mUI.confirmButton, SIGNAL(released()),
        this, SLOT(confirmAndClose())
    );

    QObject::connect(
        mUI.cancelButton, SIGNAL(released()),
        this, SLOT(close())
    );

    QObject::connect(
        mUI.deleteProfileButton, SIGNAL(released()),
        this, SLOT(deleteProfileAndDiscard())
    );

    addDockWidget(Qt::RightDockWidgetArea, mItemPropertiesDockWidget);
    addDockWidget(Qt::RightDockWidgetArea, mProfilePropertiesDockWidget);

    {
        QAction* undoAction = mUndoStack.createUndoAction(this, "Undo");
        undoAction->setIcon(getShareIcon("edit-undo.png"));
        QAction* redoAction = mUndoStack.createRedoAction(this, "Redo");
        redoAction->setIcon(getShareIcon("edit-redo.png"));

        mUI.toolBar->addAction(undoAction);
        mUI.toolBar->addAction(redoAction);
    }

    QObject::connect(
        mUI.itemsTree, SIGNAL(currentItemChanged(QTreeWidgetItem*, QTreeWidgetItem*)),
        this, SLOT(itemSelectionChanged(QTreeWidgetItem*, QTreeWidgetItem*))
    );

    QObject::connect(
        mUI.itemsTree, SIGNAL(itemChanged(QTreeWidgetItem*, int)),
        this, SLOT(itemChanged(QTreeWidgetItem*, int))
    );

    QTreeWidgetItem* benchmarkItem = new QTreeWidgetItem();
    // benchmark can't be unselected
    benchmarkItem->setFlags(
        Qt::ItemIsSelectable |
        /*Qt::ItemIsUserCheckable |*/
        Qt::ItemIsEnabled);
    mUI.itemsTree->addTopLevelItem(benchmarkItem);

    synchronizeTreeItem(benchmarkItem, xccdf_benchmark_to_item(mBenchmark), true);
    _refreshXCCDFItemChildrenDisabledState(benchmarkItem, true);

    // let title stretch and take space as the tailoring window grows
    mUI.itemsTree->header()->setResizeMode(0, QHeaderView::Stretch);

    mUI.itemsTree->expandAll();

    setWindowTitle(QString("Tailoring '%1'").arg(oscapTextIteratorGetPreferred(xccdf_profile_get_title(mProfile))));

    mItemPropertiesDockWidget->refresh();
    mProfilePropertiesDockWidget->refresh();

    {
        mUndoViewDockWidget->setWindowTitle("Undo History");
        mUndoViewDockWidget->setWidget(new QUndoView(&mUndoStack, mUndoViewDockWidget));
        addDockWidget(Qt::RightDockWidgetArea, mUndoViewDockWidget);
        mUndoViewDockWidget->hide();

        mUI.toolBar->addSeparator();
        mUI.toolBar->addAction(mUndoViewDockWidget->toggleViewAction());
    }

    // start centered
    move(QApplication::desktop()->screen()->rect().center() - rect().center());
    show();
}

TailoringWindow::~TailoringWindow()
{}

inline bool getXccdfItemInternalSelected(struct xccdf_policy* policy, struct xccdf_item* item)
{
    struct xccdf_select* select = xccdf_policy_get_select_by_id(policy, xccdf_item_get_id(item));
    return select ? xccdf_select_get_selected(select) : xccdf_item_get_selected(item);
}

void TailoringWindow::setItemSelected(struct xccdf_item* xccdfItem, bool selected)
{
    struct xccdf_select* newSelect = xccdf_select_new();
    xccdf_select_set_item(newSelect, xccdf_item_get_id(xccdfItem));
    xccdf_select_set_selected(newSelect, selected);

    xccdf_profile_add_select(mProfile, newSelect);
    xccdf_policy_add_select(mPolicy, xccdf_select_clone(newSelect));

    if (getXccdfItemInternalSelected(mPolicy, xccdfItem) != selected)
        throw TailoringWindowException(
             QString(
                 "Even though xccdf_select was added to both profile and policy "
                 "to make '%1' selected=%2, it remains selected=%3."
             ).arg(QString::fromUtf8(xccdf_item_get_id(xccdfItem))).arg(selected).arg(!selected)
        );
}

void TailoringWindow::synchronizeTreeItem(QTreeWidgetItem* treeItem, struct xccdf_item* xccdfItem, bool recursive)
{
    ++mSynchronizeItemLock;

    treeItem->setText(0, oscapTextIteratorGetPreferred(xccdf_item_get_title(xccdfItem)));

    const unsigned int typeColumn = 1;

    switch (xccdf_item_get_type(xccdfItem))
    {
        case XCCDF_BENCHMARK:
            treeItem->setIcon(0, getShareIcon("benchmark.png"));
            break;

        case XCCDF_GROUP:
            treeItem->setIcon(0, getShareIcon("group.png"));
            break;

        case XCCDF_RULE:
            treeItem->setIcon(0, getShareIcon("rule.png"));
            break;

        case XCCDF_VALUE:
            treeItem->setIcon(0, getShareIcon("value.png"));
            break;

        default:
            treeItem->setIcon(0, QIcon());
            break;
    }

    treeItem->setText(1, QString::fromUtf8(xccdf_item_get_id(xccdfItem)));
    treeItem->setData(0, Qt::UserRole, QVariant::fromValue(reinterpret_cast<void*>(xccdfItem)));

    xccdf_type_t xccdfItemType = xccdf_item_get_type(xccdfItem);
    switch (xccdfItemType)
    {
        case XCCDF_RULE:
        case XCCDF_GROUP:
        {
            treeItem->setFlags(treeItem->flags() | Qt::ItemIsUserCheckable);
            treeItem->setCheckState(0,
                    getXccdfItemInternalSelected(mPolicy, xccdfItem) ? Qt::Checked : Qt::Unchecked);
            _syncXCCDFItemChildrenDisabledState(treeItem, treeItem->checkState(0));
            break;
        }
        case XCCDF_VALUE:
            treeItem->setFlags(treeItem->flags() & ~Qt::ItemIsUserCheckable);
        default:
            break;
    }

    if (recursive)
    {
        typedef std::vector<struct xccdf_item*> XCCDFItemVector;
        typedef std::map<struct xccdf_item*, QTreeWidgetItem*> XCCDFToQtItemMap;

        XCCDFItemVector itemsToAdd;
        XCCDFToQtItemMap existingItemsMap;

        // valuesIt contains Values
        struct xccdf_value_iterator* valuesIt = NULL;
        // itemsIt contains Rules and Groups
        struct xccdf_item_iterator* itemsIt = NULL;

        switch (xccdfItemType)
        {
            case XCCDF_GROUP:
                valuesIt = xccdf_group_get_values(xccdf_item_to_group(xccdfItem));
                itemsIt = xccdf_group_get_content(xccdf_item_to_group(xccdfItem));
                break;
            case XCCDF_BENCHMARK:
                valuesIt = xccdf_benchmark_get_values(xccdf_item_to_benchmark(xccdfItem));
                itemsIt = xccdf_benchmark_get_content(xccdf_item_to_benchmark(xccdfItem));
                break;
            default:
                break;
        }

        if (valuesIt != NULL)
        {
            while (xccdf_value_iterator_has_more(valuesIt))
            {
                struct xccdf_value* childItem = xccdf_value_iterator_next(valuesIt);
                itemsToAdd.push_back(xccdf_value_to_item(childItem));
            }
            xccdf_value_iterator_free(valuesIt);
        }

        if (itemsIt != NULL)
        {
            while (xccdf_item_iterator_has_more(itemsIt))
            {
                struct xccdf_item* childItem = xccdf_item_iterator_next(itemsIt);
                itemsToAdd.push_back(childItem);
            }
            xccdf_item_iterator_free(itemsIt);
        }

        for (int i = 0; i < treeItem->childCount(); ++i)
        {
            QTreeWidgetItem* childTreeItem = treeItem->child(i);
            struct xccdf_item* childXccdfItem = getXccdfItemFromTreeItem(childTreeItem);

            if (std::find(itemsToAdd.begin(), itemsToAdd.end(), childXccdfItem) == itemsToAdd.end())
            {
                // this will remove it from the tree as well, see ~QTreeWidgetItem()
                delete childTreeItem;
            }
            else
            {
                existingItemsMap[childXccdfItem] = childTreeItem;
            }
        }

        unsigned int idx = 0;
        for (XCCDFItemVector::const_iterator it = itemsToAdd.begin();
                it != itemsToAdd.end(); ++it, ++idx)
        {
            struct xccdf_item* childXccdfItem = *it;
            QTreeWidgetItem* childTreeItem = 0;

            XCCDFToQtItemMap::iterator mapIt = existingItemsMap.find(childXccdfItem);

            if (mapIt == existingItemsMap.end())
            {
                childTreeItem = new QTreeWidgetItem();

                childTreeItem->setFlags(
                        Qt::ItemIsSelectable |
                        Qt::ItemIsEnabled);

                treeItem->insertChild(idx, childTreeItem);
            }
            else
            {
                childTreeItem = mapIt->second;
            }

            synchronizeTreeItem(childTreeItem, childXccdfItem, true);
        }
    }

    --mSynchronizeItemLock;
}

void TailoringWindow::setValueValue(struct xccdf_value* xccdfValue, const QString& newValue)
{
    struct xccdf_setvalue* setvalue = xccdf_setvalue_new();
    xccdf_setvalue_set_item(setvalue, xccdf_value_get_id(xccdfValue));
    xccdf_setvalue_set_value(setvalue, newValue.toUtf8().constData());

    xccdf_profile_add_setvalue(mProfile, setvalue);

    assert(getCurrentValueValue(xccdfValue) == newValue);
}

void TailoringWindow::refreshXccdfItemPropertiesDockWidget()
{
    mItemPropertiesDockWidget->refresh();
}

QString TailoringWindow::getCurrentValueValue(struct xccdf_value* xccdfValue)
{
    return QString::fromUtf8(xccdf_policy_get_value_of_item(mPolicy, xccdf_value_to_item(xccdfValue)));
}

void TailoringWindow::setValueValueWithUndoCommand(struct xccdf_value* xccdfValue, const QString& newValue)
{
    mUndoStack.push(new XCCDFValueChangeUndoCommand(this, xccdfValue, newValue, getCurrentValueValue(xccdfValue)));
}

QString TailoringWindow::getProfileID() const
{
    return QString::fromUtf8(xccdf_profile_get_id(mProfile));
}

void TailoringWindow::setProfileTitle(const QString& title)
{
    struct oscap_text_iterator* titles = xccdf_profile_get_title(mProfile);
    struct oscap_text* titleText = 0;
    while (oscap_text_iterator_has_more(titles))
    {
        struct oscap_text* titleCandidate = oscap_text_iterator_next(titles);
        if (!titleText || strcmp(oscap_text_get_lang(titleCandidate), OSCAP_LANG_DEFAULT) == 0)
            titleText = titleCandidate;
    }
    oscap_text_iterator_free(titles);

    if (titleText)
    {
        oscap_text_set_text(titleText, title.toUtf8().constData());
        oscap_text_set_lang(titleText, OSCAP_LANG_DEFAULT);
    }
    else
    {
        // FIXME: we cannot add new title using this API :-(
        throw TailoringWindowException("Not suitable oscap_text found that we could edit to change profile title.");
    }

    assert(getProfileTitle() == title);
}

QString TailoringWindow::getProfileTitle() const
{
    return oscapTextIteratorGetPreferred(xccdf_profile_get_title(mProfile));
}

void TailoringWindow::setProfileTitleWithUndoCommand(const QString& newTitle)
{
    mUndoStack.push(new ProfileTitleChangeUndoCommand(this, getProfileTitle(), newTitle));
}

void TailoringWindow::setProfileDescription(const QString& description)
{
    struct oscap_text_iterator* descriptions = xccdf_profile_get_description(mProfile);
    struct oscap_text* descText = 0;
    while (oscap_text_iterator_has_more(descriptions))
    {
        struct oscap_text* descCandidate = oscap_text_iterator_next(descriptions);
        if (!descText || strcmp(oscap_text_get_lang(descCandidate), OSCAP_LANG_DEFAULT) == 0)
            descText = descCandidate;
    }
    oscap_text_iterator_free(descriptions);

    if (descText)
    {
        oscap_text_set_text(descText, description.toUtf8().constData());
        oscap_text_set_lang(descText, OSCAP_LANG_DEFAULT);
    }
    else
    {
        // FIXME: we cannot add new title using this API :-(
        throw TailoringWindowException("Not suitable oscap_text found that we could edit to change profile description.");
    }

    assert(getProfileDescription() == description);
}

QString TailoringWindow::getProfileDescription() const
{
    return oscapTextIteratorGetPreferred(xccdf_profile_get_description(mProfile));
}

void TailoringWindow::setProfileDescriptionWithUndoCommand(const QString& newDescription)
{
    mUndoStack.push(new ProfileDescriptionChangeUndoCommand(this, getProfileDescription(), newDescription));
}

void TailoringWindow::refreshProfileDockWidget()
{
    mProfilePropertiesDockWidget->refresh();
}

void TailoringWindow::confirmAndClose()
{
    mChangesConfirmed = true;

    close();
}

void TailoringWindow::deleteProfileAndDiscard()
{
    mChangesConfirmed = false;
    mNewProfile = true;

    close();
}

void TailoringWindow::closeEvent(QCloseEvent * event)
{
    if (!mChangesConfirmed)
    {
        if (QMessageBox::question(this, "Discard changes?",
            "Are you sure you want to discard all changes performed in this tailoring window.",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::No)
        {
            event->ignore();
            return;
        }

        // undo everything
        mUndoStack.setIndex(0);
        // TODO: Delete the profile if it was created as a tailoring action
    }

    QMainWindow::closeEvent(event);

    // TODO: This is the only place where we depend on MainWindow which really sucks
    //       and makes this code more spaghetti-fied. Ideally MainWindow would handle
    //       this connection but there are no signals for window closure, the only
    //       way to react is to reimplement closeEvent... This needs further research.

    if (mParentMainWindow)
    {
        mParentMainWindow->notifyTailoringFinished(mNewProfile, mChangesConfirmed);
    }
}

void TailoringWindow::itemSelectionChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    struct xccdf_item* item = getXccdfItemFromTreeItem(current);
    mItemPropertiesDockWidget->setXccdfItem(item, mPolicy);
}

void TailoringWindow::itemChanged(QTreeWidgetItem* treeItem, int column)
{
    if (mSynchronizeItemLock > 0)
        return;

    const bool checkState = treeItem->checkState(0) == Qt::Checked;

    struct xccdf_item* xccdfItem = getXccdfItemFromTreeItem(treeItem);
    if (!xccdfItem)
        return;

    if (xccdf_item_get_type(xccdfItem) == XCCDF_VALUE)
        return;

    const bool itemCheckState = getXccdfItemInternalSelected(mPolicy, xccdfItem);

    if (checkState != itemCheckState)
        mUndoStack.push(new XCCDFItemSelectUndoCommand(this, treeItem, checkState));

    _syncXCCDFItemChildrenDisabledState(treeItem, checkState);
}
