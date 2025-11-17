#include <BBBuilder/Controls/InspectorPanel.h>

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QVBoxLayout>

namespace BBB {

InspectorPanel::InspectorPanel(QWidget *parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("inspectorPanel"));
    setStyleSheet(QStringLiteral(
        "#inspectorPanel { background:rgba(7,15,23,0.96); border:1px solid rgba(255,255,255,0.05); border-radius:18px; }"
        "#inspectorPanel QLabel { color:#e5f0ff; }"
        "QLineEdit, QTextEdit, QComboBox, QDoubleSpinBox { background:rgba(9,18,32,0.92); color:#e5f0ff; border:1px solid rgba(126,196,255,0.3); border-radius:8px; padding:4px 8px; }"
        "QComboBox QAbstractItemView { background-color:#091220; color:#e5f0ff; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(18);

    auto makeCaption = [](QWidget *parent, const QString &text) {
        auto *lbl = new QLabel(text, parent);
        lbl->setStyleSheet(QStringLiteral("color:#8fa9c6; font-size:11px; letter-spacing:0.08em; text-transform:uppercase; margin-top:6px;"));
        return lbl;
    };

    moduleSection_ = new QWidget(this);
    auto *moduleLayout = new QVBoxLayout(moduleSection_);
    auto *moduleHeader = new QLabel(tr("Module"), moduleSection_);
    moduleHeader->setStyleSheet(QStringLiteral("font-size:16px; font-weight:600;"));
    moduleLabelEdit_ = new QLineEdit(moduleSection_);
    moduleLabelEdit_->setPlaceholderText(tr("Display label"));
    moduleDescriptionEdit_ = new QTextEdit(moduleSection_);
    moduleDescriptionEdit_->setPlaceholderText(tr("Description / notes"));
    moduleDescriptionEdit_->setFixedHeight(80);
    moduleApplyButton_ = new QPushButton(tr("Apply Module Changes"), moduleSection_);
    auto *futureRow = new QHBoxLayout();
    auto *autoLayoutBtn = new QPushButton(tr("Auto layout"), moduleSection_);
    autoLayoutBtn->setEnabled(false);
    auto *smartLinkBtn = new QPushButton(tr("Smart link"), moduleSection_);
    smartLinkBtn->setEnabled(false);
    futureRow->addWidget(autoLayoutBtn);
    futureRow->addWidget(smartLinkBtn);
    moduleLayout->addWidget(moduleHeader);
    moduleLayout->addWidget(makeCaption(moduleSection_, tr("Display label")));
    moduleLayout->addWidget(moduleLabelEdit_);
    moduleLayout->addWidget(makeCaption(moduleSection_, tr("Description / notes")));
    moduleLayout->addWidget(moduleDescriptionEdit_);
    moduleLayout->addWidget(moduleApplyButton_);
    moduleLayout->addLayout(futureRow);
    root->addWidget(moduleSection_);

    controlSection_ = new QWidget(this);
    auto *controlLayout = new QVBoxLayout(controlSection_);
    auto *controlHeader = new QLabel(tr("Control"), controlSection_);
    controlHeader->setStyleSheet(QStringLiteral("font-size:16px; font-weight:600;"));
    controlSelector_ = new QComboBox(controlSection_);
    controlSelector_->setPlaceholderText(tr("Select control"));
    controlLabelEdit_ = new QLineEdit(controlSection_);
    controlLabelEdit_->setPlaceholderText(tr("Label"));
    controlDescriptionEdit_ = new QTextEdit(controlSection_);
    controlDescriptionEdit_->setPlaceholderText(tr("Description / notes"));
    controlDescriptionEdit_->setFixedHeight(64);
    controlTypeCombo_ = new QComboBox(controlSection_);
    controlTypeCombo_->addItems({tr("Slider"), tr("Knob"), tr("Toggle"), tr("Fader"), tr("Meter")});

    auto *rangeRow = new QHBoxLayout();
    controlMinSpin_ = new QDoubleSpinBox(controlSection_);
    controlMinSpin_->setDecimals(3);
    controlMinSpin_->setRange(-100000, 100000);
    controlMaxSpin_ = new QDoubleSpinBox(controlSection_);
    controlMaxSpin_->setDecimals(3);
    controlMaxSpin_->setRange(-100000, 100000);
    controlStepSpin_ = new QDoubleSpinBox(controlSection_);
    controlStepSpin_->setDecimals(3);
    controlStepSpin_->setRange(0.0001, 1000);
    controlStepSpin_->setSingleStep(0.1);
    rangeRow->addWidget(new QLabel(tr("Min"), controlSection_));
    rangeRow->addWidget(controlMinSpin_);
    rangeRow->addWidget(new QLabel(tr("Max"), controlSection_));
    rangeRow->addWidget(controlMaxSpin_);
    rangeRow->addWidget(new QLabel(tr("Step"), controlSection_));
    rangeRow->addWidget(controlStepSpin_);

    auto *defaultRow = new QHBoxLayout();
    controlDefaultSpin_ = new QDoubleSpinBox(controlSection_);
    controlDefaultSpin_->setDecimals(3);
    controlDefaultSpin_->setRange(-100000, 100000);
    defaultRow->addWidget(controlDefaultSpin_);
    defaultRow->addStretch();

    controlUnitEdit_ = new QLineEdit(controlSection_);
    controlUnitEdit_->setPlaceholderText(tr("Unit (dB, Hz, %...)"));
    controlParamCombo_ = new QComboBox(controlSection_);
    controlParamCombo_->setPlaceholderText(tr("Parameter mapping"));

    auto *addrRow = new QHBoxLayout();
    controlAddressEdit_ = new QLineEdit(controlSection_);
    controlAddressEdit_->setPlaceholderText(tr("Address (hex or decimal)"));
    controlBytesCombo_ = new QComboBox(controlSection_);
    controlBytesCombo_->setMinimumWidth(90);
    controlBytesCombo_->addItem(tr("1 byte"), 1);
    controlBytesCombo_->addItem(tr("2 bytes"), 2);
    controlBytesCombo_->addItem(tr("3 bytes"), 3);
    controlBytesCombo_->addItem(tr("4 bytes"), 4);
    controlFormatCombo_ = new QComboBox(controlSection_);
    controlFormatCombo_->addItem(tr("Fixed 5.23"), QStringLiteral("fixed5.23"));
    controlFormatCombo_->addItem(tr("Unsigned 8-bit"), QStringLiteral("u8"));
    controlFormatCombo_->addItem(tr("Unsigned 16-bit"), QStringLiteral("u16"));
    controlFormatCombo_->addItem(tr("Unsigned 24-bit"), QStringLiteral("u24"));
    controlFormatCombo_->addItem(tr("Unsigned 32-bit"), QStringLiteral("u32"));
    controlFormatCombo_->addItem(tr("Raw (passthrough)"), QStringLiteral("raw"));
    controlReadOnlyCheck_ = new QCheckBox(tr("Read-only"), controlSection_);
    addrRow->addWidget(controlAddressEdit_, 2);
    addrRow->addWidget(controlBytesCombo_);
    addrRow->addWidget(controlFormatCombo_);
    addrRow->addWidget(controlReadOnlyCheck_);

    controlApplyButton_ = new QPushButton(tr("Apply Control Changes"), controlSection_);
    controlDeleteButton_ = new QPushButton(tr("Delete Control"), controlSection_);
    controlDeleteButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background-color:#7a2432; color:white; }"
        "QPushButton:disabled { background-color:rgba(255,255,255,0.1); color:rgba(255,255,255,0.3); }"));
    auto *futureControlRow = new QHBoxLayout();
    auto *morphBtn = new QPushButton(tr("Morph target"), controlSection_);
    morphBtn->setEnabled(false);
    auto *automationBtn = new QPushButton(tr("Automation lanes"), controlSection_);
    automationBtn->setEnabled(false);
    futureControlRow->addWidget(morphBtn);
    futureControlRow->addWidget(automationBtn);

    controlLayout->addWidget(controlHeader);
    controlLayout->addWidget(makeCaption(controlSection_, tr("Select control")));
    controlLayout->addWidget(controlSelector_);
    controlLayout->addWidget(makeCaption(controlSection_, tr("Custom label")));
    controlLayout->addWidget(controlLabelEdit_);
    controlLayout->addWidget(makeCaption(controlSection_, tr("Description")));
    controlLayout->addWidget(controlDescriptionEdit_);
    controlLayout->addWidget(makeCaption(controlSection_, tr("Control type")));
    controlLayout->addWidget(controlTypeCombo_);
    controlLayout->addWidget(makeCaption(controlSection_, tr("Range")));
    controlLayout->addLayout(rangeRow);
    controlLayout->addWidget(makeCaption(controlSection_, tr("Default value")));
    controlLayout->addLayout(defaultRow);
    controlLayout->addWidget(makeCaption(controlSection_, tr("Unit")));
    controlLayout->addWidget(controlUnitEdit_);
    controlLayout->addWidget(makeCaption(controlSection_, tr("Parameter mapping")));
    controlLayout->addWidget(controlParamCombo_);
    controlLayout->addWidget(makeCaption(controlSection_, tr("Address & Format")));
    controlLayout->addLayout(addrRow);
    auto *controlButtons = new QHBoxLayout();
    controlButtons->addWidget(controlApplyButton_);
    controlButtons->addStretch();
    controlButtons->addWidget(controlDeleteButton_);
    controlLayout->addLayout(controlButtons);
    controlLayout->addLayout(futureControlRow);
    root->addWidget(controlSection_);

    root->addStretch();

    connect(moduleApplyButton_, &QPushButton::clicked, this, [this]() {
        emit moduleUpdated(currentModuleName_, moduleLabelEdit_->text(), moduleDescriptionEdit_->toPlainText());
    });

    connect(controlApplyButton_, &QPushButton::clicked, this, [this]() {
        if (currentModuleName_.isEmpty() || currentControlOriginalId_.isEmpty()) return;
        ControlDescriptor updated = currentControl_;
        updated.label = controlLabelEdit_->text();
        updated.description = controlDescriptionEdit_->toPlainText().trimmed();
        updated.unit = controlUnitEdit_->text();
        updated.type = controlTypeCombo_->currentText().toLower();
        updated.min = controlMinSpin_->value();
        updated.max = controlMaxSpin_->value();
        updated.step = controlStepSpin_->value();
        updated.defaultValue = controlDefaultSpin_->value();
        updated.readOnly = controlReadOnlyCheck_->isChecked();

        QString addressText = controlAddressEdit_->text().trimmed();
        bool addressOk = true;
        if (addressText.isEmpty()) {
            updated.address = 0;
        } else {
            bool ok = false;
            if (addressText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
                updated.address = addressText.mid(2).toUInt(&ok, 16);
            } else {
                updated.address = addressText.toUInt(&ok, 10);
            }
            addressOk = ok;
        }

        const int bytesIdx = controlBytesCombo_->currentIndex();
        if (bytesIdx >= 0) {
            updated.byteWidth = static_cast<quint8>(controlBytesCombo_->itemData(bytesIdx).toInt());
        }
        const QString format = controlFormatCombo_->currentData().toString();
        updated.format = format.isEmpty() ? QStringLiteral("fixed5.23") : format;

        QStringList errors;
        if (updated.label.trimmed().isEmpty()) {
            errors << tr("Label cannot be empty.");
        }
        if (updated.max <= updated.min) {
            errors << tr("Max must be greater than min.");
        }
        if (updated.step <= 0.0) {
            errors << tr("Step must be positive.");
        }
        if (updated.defaultValue < updated.min || updated.defaultValue > updated.max) {
            errors << tr("Default value must be within the min/max range.");
        }
        if (!addressOk) {
            errors << tr("Address must be a valid hex or decimal number.");
        }
        if (updated.byteWidth < 1 || updated.byteWidth > 4) {
            errors << tr("Byte width must be between 1 and 4.");
        }

        if (!errors.isEmpty()) {
            QMessageBox::warning(this, tr("Invalid control"), errors.join('\n'));
            return;
        }

        const QString paramChoice = controlParamCombo_->currentData().toString();
        if (!paramChoice.isEmpty()) {
            updated.id = paramChoice;
            for (const auto &candidate : moduleControls_) {
                if (candidate.id == paramChoice) {
                    updated.address = candidate.address;
                    updated.byteWidth = candidate.byteWidth;
                    updated.format = candidate.format;
                    updated.readOnly = candidate.readOnly;
                    break;
                }
            }
        }
        emit controlUpdated(currentModuleName_, currentControlOriginalId_, updated);
    });

    connect(controlDeleteButton_, &QPushButton::clicked, this, [this]() {
        if (currentModuleName_.isEmpty() || currentControlOriginalId_.isEmpty()) return;
        const auto reply = QMessageBox::question(this,
                                                 tr("Delete control"),
                                                 tr("Remove control \"%1\" from %2?").arg(currentControlOriginalId_, currentModuleName_),
                                                 QMessageBox::Yes | QMessageBox::No,
                                                 QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
        emit controlDeleted(currentModuleName_, currentControlOriginalId_);
    });

    connect(controlSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0 || index >= moduleControls_.size()) return;
        showControl(currentModuleName_, moduleControls_.at(index));
    });

    clearSelection();
}

void InspectorPanel::clearSelection() {
    currentModuleName_.clear();
    currentControlOriginalId_.clear();
    moduleControls_.clear();
    moduleSection_->setDisabled(true);
    controlSection_->setDisabled(true);
    moduleLabelEdit_->clear();
    moduleDescriptionEdit_->clear();
    controlSelector_->clear();
    controlLabelEdit_->clear();
    controlDescriptionEdit_->clear();
    controlUnitEdit_->clear();
    controlDefaultSpin_->setValue(0.0);
    controlAddressEdit_->clear();
    controlBytesCombo_->setCurrentIndex(3);
    controlFormatCombo_->setCurrentIndex(0);
    controlReadOnlyCheck_->setChecked(false);
    controlParamCombo_->clear();
    if (controlDeleteButton_) controlDeleteButton_->setEnabled(false);
}

void InspectorPanel::showModule(const QString &moduleName, const ModuleDescriptor &module, const QString &displayLabel) {
    currentModuleName_ = moduleName;
    moduleControls_ = module.controls;
    moduleSection_->setDisabled(false);
    moduleLabelEdit_->setText(displayLabel);
    moduleDescriptionEdit_->setText(module.description);

    populateControlSelector(module.controls);
    controlSection_->setDisabled(module.controls.isEmpty());
    if (!module.controls.isEmpty()) {
        showControl(moduleName, module.controls.first());
    } else {
        currentControlOriginalId_.clear();
    }
}

void InspectorPanel::showControl(const QString &moduleName, const ControlDescriptor &control) {
    currentModuleName_ = moduleName;
    currentControl_ = control;
    currentControlOriginalId_ = control.id;
    controlSection_->setDisabled(false);
    if (controlDeleteButton_) controlDeleteButton_->setEnabled(true);
    controlLabelEdit_->setText(control.label);
    controlDescriptionEdit_->setPlainText(control.description);
    controlUnitEdit_->setText(control.unit);
    controlMinSpin_->setValue(control.min);
    controlMaxSpin_->setValue(control.max);
    controlStepSpin_->setValue(control.step);
    controlDefaultSpin_->setRange(control.min, control.max);
    const double clampedDefault = std::clamp(control.defaultValue, control.min, control.max);
    controlDefaultSpin_->setValue(clampedDefault);
    int typeIdx = controlTypeCombo_->findText(control.type.left(1).toUpper() + control.type.mid(1));
    if (typeIdx >= 0) controlTypeCombo_->setCurrentIndex(typeIdx);

    if (control.address == 0) {
        controlAddressEdit_->clear();
    } else {
        controlAddressEdit_->setText(QStringLiteral("0x%1").arg(control.address, 0, 16).toUpper());
    }
    int bytesIdx = controlBytesCombo_->findData(control.byteWidth);
    if (bytesIdx < 0) bytesIdx = controlBytesCombo_->findData(4);
    controlBytesCombo_->setCurrentIndex(bytesIdx);
    const QString format = control.format.isEmpty() ? QStringLiteral("fixed5.23") : control.format.toLower();
    int formatIdx = -1;
    for (int i = 0; i < controlFormatCombo_->count(); ++i) {
        if (controlFormatCombo_->itemData(i).toString().compare(format, Qt::CaseInsensitive) == 0) {
            formatIdx = i;
            break;
        }
    }
    if (formatIdx < 0) {
        controlFormatCombo_->addItem(format, format);
        formatIdx = controlFormatCombo_->count() - 1;
    }
    controlFormatCombo_->setCurrentIndex(formatIdx);
    controlReadOnlyCheck_->setChecked(control.readOnly);

    populateParameterMappings(control);
}

void InspectorPanel::focusControl(const QString &moduleName, const QString &controlId) {
    if (moduleName != currentModuleName_ || controlId.isEmpty()) return;
    const int index = controlSelector_->findData(controlId);
    if (index < 0 || index >= moduleControls_.size()) return;
    QSignalBlocker blocker(controlSelector_);
    controlSelector_->setCurrentIndex(index);
    showControl(moduleName, moduleControls_.at(index));
}

void InspectorPanel::populateControlSelector(const QVector<ControlDescriptor> &controls) {
    QSignalBlocker blocker(controlSelector_);
    controlSelector_->clear();
    for (const auto &ctrl : controls) {
        const QString entry = QStringLiteral("%1 · %2").arg(ctrl.label, ctrl.id);
        controlSelector_->addItem(entry, ctrl.id);
    }
    if (!controls.isEmpty()) {
        controlSelector_->setCurrentIndex(0);
    } else {
        controlSelector_->setCurrentIndex(-1);
    }
}

void InspectorPanel::populateParameterMappings(const ControlDescriptor &control) {
    auto *model = qobject_cast<QStandardItemModel *>(controlParamCombo_->model());
    if (!model) {
        controlParamCombo_->clear();
        return;
    }
    model->clear();
    int targetIndex = -1;
    for (int i = 0; i < moduleControls_.size(); ++i) {
        const auto &candidate = moduleControls_.at(i);
        auto *item = new QStandardItem(QStringLiteral("%1 · %2").arg(candidate.label, candidate.id));
        item->setData(candidate.id, Qt::UserRole);
        bool compatible = isMappingCompatible(control, candidate);
        if (!compatible && candidate.id != control.id) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(tr("Format/width mismatch (%1, %2 bytes)")
                                 .arg(candidate.format.isEmpty() ? tr("n/a") : candidate.format)
                                 .arg(candidate.byteWidth));
        } else if (candidate.id == control.id) {
            targetIndex = i;
        }
        model->appendRow(item);
    }
    if (model->rowCount() == 0) {
        controlParamCombo_->setCurrentIndex(-1);
        return;
    }
    if (targetIndex < 0) {
        targetIndex = 0;
    }
    controlParamCombo_->setCurrentIndex(targetIndex);
}

bool InspectorPanel::isMappingCompatible(const ControlDescriptor &lhs, const ControlDescriptor &rhs) const {
    if (rhs.id == lhs.id) return true;
    if (rhs.readOnly) return false;
    if (!lhs.format.isEmpty() && !rhs.format.isEmpty() && lhs.format != rhs.format) return false;
    if (lhs.byteWidth != rhs.byteWidth) return false;
    return true;
}

} // namespace BBB
