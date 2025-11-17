#pragma once

#include <QFrame>
#include <QStringList>
#include <QVector>

#include <BBBuilder/Project.h>

QT_BEGIN_NAMESPACE
class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QTextEdit;
class QCheckBox;
class QSpinBox;
QT_END_NAMESPACE

namespace BBB {

class InspectorPanel : public QFrame {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget *parent = nullptr);

    void clearSelection();
    void showModule(const QString &moduleName, const ModuleDescriptor &module, const QString &displayLabel);
    void showControl(const QString &moduleName, const ControlDescriptor &control);
    void focusControl(const QString &moduleName, const QString &controlId);

signals:
    void moduleUpdated(const QString &moduleName, const QString &displayLabel, const QString &description);
    void controlUpdated(const QString &moduleName, const QString &previousId, const ControlDescriptor &control);
    void controlDeleted(const QString &moduleName, const QString &controlId);

private:
    void buildModuleUi();
    void buildControlUi();

    QWidget *moduleSection_ = nullptr;
    QWidget *controlSection_ = nullptr;

    QLineEdit *moduleLabelEdit_ = nullptr;
    QTextEdit *moduleDescriptionEdit_ = nullptr;
    QPushButton *moduleApplyButton_ = nullptr;

    QComboBox *controlSelector_ = nullptr;
    QLineEdit *controlLabelEdit_ = nullptr;
    QTextEdit *controlDescriptionEdit_ = nullptr;
    QComboBox *controlTypeCombo_ = nullptr;
    QDoubleSpinBox *controlMinSpin_ = nullptr;
    QDoubleSpinBox *controlMaxSpin_ = nullptr;
    QDoubleSpinBox *controlStepSpin_ = nullptr;
    QDoubleSpinBox *controlDefaultSpin_ = nullptr;
    QLineEdit *controlUnitEdit_ = nullptr;
    QComboBox *controlParamCombo_ = nullptr;
    QLineEdit *controlAddressEdit_ = nullptr;
    QComboBox *controlBytesCombo_ = nullptr;
    QComboBox *controlFormatCombo_ = nullptr;
    QCheckBox *controlReadOnlyCheck_ = nullptr;
    QPushButton *controlApplyButton_ = nullptr;
    QPushButton *controlDeleteButton_ = nullptr;

    QString currentModuleName_;
    QString currentControlOriginalId_;
    QVector<ControlDescriptor> moduleControls_;
    ControlDescriptor currentControl_;

    void populateControlSelector(const QVector<ControlDescriptor> &controls);
    void populateParameterMappings(const ControlDescriptor &control);
    bool isMappingCompatible(const ControlDescriptor &lhs, const ControlDescriptor &rhs) const;
};

} // namespace BBB
