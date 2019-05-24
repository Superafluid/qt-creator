/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "runconfiguration.h"

#include "project.h"
#include "runcontrol.h"
#include "target.h"
#include "toolchain.h"
#include "abi.h"
#include "buildconfiguration.h"
#include "environmentaspect.h"
#include "kitinformation.h"
#include "runconfigurationaspects.h"
#include "session.h"
#include "kitinformation.h"

#include <utils/algorithm.h>
#include <utils/checkablemessagebox.h>
#include <utils/detailswidget.h>
#include <utils/outputformatter.h>
#include <utils/qtcassert.h>
#include <utils/utilsicons.h>

#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/variablechooser.h>

#include <QDir>
#include <QFormLayout>
#include <QHash>
#include <QPushButton>
#include <QTimer>
#include <QLoggingCategory>
#include <QSettings>

using namespace Utils;
using namespace ProjectExplorer::Internal;

namespace ProjectExplorer {

///////////////////////////////////////////////////////////////////////
//
// ISettingsAspect
//
///////////////////////////////////////////////////////////////////////

ISettingsAspect::ISettingsAspect(const ConfigWidgetCreator &creator)
    : m_configWidgetCreator(creator)
{}

QWidget *ISettingsAspect::createConfigWidget() const
{
    QTC_ASSERT(m_configWidgetCreator, return nullptr);
    return m_configWidgetCreator();
}

///////////////////////////////////////////////////////////////////////
//
// IRunConfigurationAspect
//
///////////////////////////////////////////////////////////////////////

GlobalOrProjectAspect::GlobalOrProjectAspect() = default;

GlobalOrProjectAspect::~GlobalOrProjectAspect()
{
    delete m_projectSettings;
}

void GlobalOrProjectAspect::setProjectSettings(ISettingsAspect *settings)
{
    m_projectSettings = settings;
}

void GlobalOrProjectAspect::setGlobalSettings(ISettingsAspect *settings)
{
    m_globalSettings = settings;
}

void GlobalOrProjectAspect::setUsingGlobalSettings(bool value)
{
    m_useGlobalSettings = value;
}

ISettingsAspect *GlobalOrProjectAspect::currentSettings() const
{
   return m_useGlobalSettings ? m_globalSettings : m_projectSettings;
}

void GlobalOrProjectAspect::fromMap(const QVariantMap &map)
{
    if (m_projectSettings)
        m_projectSettings->fromMap(map);
    m_useGlobalSettings = map.value(m_id.toString() + QLatin1String(".UseGlobalSettings"), true).toBool();
}

void GlobalOrProjectAspect::toMap(QVariantMap &map) const
{
    if (m_projectSettings)
        m_projectSettings->toMap(map);
    map.insert(m_id.toString() + QLatin1String(".UseGlobalSettings"), m_useGlobalSettings);
}

void GlobalOrProjectAspect::resetProjectToGlobalSettings()
{
    QTC_ASSERT(m_globalSettings, return);
    QVariantMap map;
    m_globalSettings->toMap(map);
    if (m_projectSettings)
        m_projectSettings->fromMap(map);
}


/*!
    \class ProjectExplorer::RunConfiguration
    \brief The RunConfiguration class is the base class for a run configuration.

    A run configuration specifies how a target should be run, while a runner
    does the actual running.

    The target owns the RunConfiguraitons and a RunControl will need to copy all
    necessary data as the RunControl may continue to exist after the RunConfiguration
    has been destroyed.

    A RunConfiguration disables itself when the project is parsing or has no parsing
    data available. The disabledReason() method can be used to get a user-facing string
    describing why the RunConfiguration considers itself unfit for use.

    Override updateEnabledState() to change the enabled state handling. Override
    disabledReasons() to provide better/more descriptions to the user.

    Connect signals that may change enabled state of your RunConfiguration to updateEnabledState.
*/

static std::vector<RunConfiguration::AspectFactory> theAspectFactories;

RunConfiguration::RunConfiguration(Target *target, Core::Id id)
    : ProjectConfiguration(target, id)
{
    connect(target->project(), &Project::parsingStarted,
            this, [this]() { updateEnabledState(); });
    connect(target->project(), &Project::parsingFinished,
            this, [this]() { updateEnabledState(); });

    connect(target, &Target::addedRunConfiguration,
            this, [this](const RunConfiguration *rc) {
        if (rc == this)
            updateEnabledState();
    });

    connect(this, &RunConfiguration::enabledChanged,
            this, &RunConfiguration::requestRunActionsUpdate);

    Utils::MacroExpander *expander = macroExpander();
    expander->setDisplayName(tr("Run Settings"));
    expander->setAccumulating(true);
    expander->registerSubProvider([target] {
        BuildConfiguration *bc = target->activeBuildConfiguration();
        return bc ? bc->macroExpander() : target->macroExpander();
    });
    expander->registerPrefix("CurrentRun:Env", tr("Variables in the current run environment"),
                             [this](const QString &var) {
        const auto envAspect = aspect<EnvironmentAspect>();
        return envAspect ? envAspect->environment().value(var) : QString();
    });

    expander->registerVariable(Constants::VAR_CURRENTRUN_WORKINGDIR,
                               tr("The currently active run configuration's working directory"),
                               [this, expander] {
        const auto wdAspect = aspect<WorkingDirectoryAspect>();
        return wdAspect ? wdAspect->workingDirectory(expander).toString() : QString();
    });

    expander->registerVariable(Constants::VAR_CURRENTRUN_NAME,
            QCoreApplication::translate("ProjectExplorer", "The currently active run configuration's name."),
            [this] { return displayName(); }, false);

    for (const AspectFactory &factory : theAspectFactories)
        m_aspects.append(factory(target));

    m_executableGetter = [this] {
        if (const auto executableAspect = aspect<ExecutableAspect>())
            return executableAspect->executable();
        return FileName();
    };
}

RunConfiguration::~RunConfiguration() = default;

bool RunConfiguration::isActive() const
{
    return target()->isActive() && target()->activeRunConfiguration() == this;
}

void RunConfiguration::setEnabled(bool enabled)
{
    if (enabled == m_isEnabled)
        return;
    m_isEnabled = enabled;
    emit enabledChanged();
}

QString RunConfiguration::disabledReason() const
{
    if (target()->project()->isParsing())
        return tr("The Project is currently being parsed.");
    if (!target()->project()->hasParsingData()) {
        QString msg = tr("The project could not be fully parsed.");
        const FileName projectFilePath = buildTargetInfo().projectFilePath;
        if (!projectFilePath.exists())
            msg += '\n' + tr("The project file \"%1\" does not exist.").arg(projectFilePath.toString());
        return msg;
    }
    return QString();
}

QWidget *RunConfiguration::createConfigurationWidget()
{
    auto widget = new QWidget;
    auto formLayout = new QFormLayout(widget);
    formLayout->setMargin(0);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    for (ProjectConfigurationAspect *aspect : m_aspects) {
        if (aspect->isVisible())
            aspect->addToConfigurationLayout(formLayout);
    }

    Core::VariableChooser::addSupportForChildWidgets(widget, macroExpander());

    auto detailsWidget = new Utils::DetailsWidget;
    detailsWidget->setState(DetailsWidget::NoSummary);
    detailsWidget->setWidget(widget);
    return detailsWidget;
}

void RunConfiguration::updateEnabledState()
{
    Project *p = target()->project();

    setEnabled(!p->isParsing() && p->hasParsingData());
}

void RunConfiguration::addAspectFactory(const AspectFactory &aspectFactory)
{
    theAspectFactories.push_back(aspectFactory);
}

/*!
 * Returns the RunConfiguration of the currently active target
 * of the startup project, if such exists, or \c nullptr otherwise.
 */

RunConfiguration *RunConfiguration::startupRunConfiguration()
{
    if (Project *pro = SessionManager::startupProject()) {
        if (const Target *target = pro->activeTarget())
            return target->activeRunConfiguration();
    }
    return nullptr;
}

bool RunConfiguration::isConfigured() const
{
    return true;
}

RunConfiguration::ConfigurationState RunConfiguration::ensureConfigured(QString *errorMessage)
{
    if (isConfigured())
        return Configured;
    if (errorMessage)
        *errorMessage = tr("Unknown error.");
    return UnConfigured;
}

BuildConfiguration *RunConfiguration::activeBuildConfiguration() const
{
    if (!target())
        return nullptr;
    return target()->activeBuildConfiguration();
}

Target *RunConfiguration::target() const
{
    return static_cast<Target *>(parent());
}

Project *RunConfiguration::project() const
{
    return target()->project();
}

QVariantMap RunConfiguration::toMap() const
{
    QVariantMap map = ProjectConfiguration::toMap();

    // FIXME: Remove this id mangling, e.g. by using a separate entry for the build key.
    if (!m_buildKey.isEmpty()) {
        const Core::Id mangled = id().withSuffix(m_buildKey);
        map.insert(settingsIdKey(), mangled.toSetting());
    }

    return map;
}

void RunConfiguration::setExecutableGetter(const RunConfiguration::ExecutableGetter &exeGetter)
{
    m_executableGetter = exeGetter;
}

FileName RunConfiguration::executable() const
{
    return m_executableGetter();
}

BuildTargetInfo RunConfiguration::buildTargetInfo() const
{
    return target()->buildTarget(m_buildKey);
}

bool RunConfiguration::fromMap(const QVariantMap &map)
{
    if (!ProjectConfiguration::fromMap(map))
        return false;

    // FIXME: Remove this id mangling, e.g. by using a separate entry for the build key.
    const Core::Id mangledId = Core::Id::fromSetting(map.value(settingsIdKey()));
    m_buildKey = mangledId.suffixAfter(id());

    return  true;
}

/*!
    \class ProjectExplorer::IRunConfigurationAspect

    \brief The IRunConfigurationAspect class provides an additional
    configuration aspect.

    Aspects are a mechanism to add RunControl-specific options to a run
    configuration without subclassing the run configuration for every addition.
    This prevents a combinatorial explosion of subclasses and eliminates
    the need to add all options to the base class.
*/

/*!
    \internal

    \class ProjectExplorer::Runnable

    \brief The ProjectExplorer::Runnable class wraps information needed
    to execute a process on a target device.

    A target specific \l RunConfiguration implementation can specify
    what information it considers necessary to execute a process
    on the target. Target specific) \n RunWorker implementation
    can use that information either unmodified or tweak it or ignore
    it when setting up a RunControl.

    From Qt Creator's core perspective a Runnable object is opaque.
*/

/*!
    \internal

    \brief Returns a \l Runnable described by this RunConfiguration.
*/

Runnable RunConfiguration::runnable() const
{
    Runnable r;
    r.executable = executable().toString();
    if (auto argumentsAspect = aspect<ArgumentsAspect>())
        r.commandLineArguments = argumentsAspect->arguments(macroExpander());
    if (auto workingDirectoryAspect = aspect<WorkingDirectoryAspect>())
        r.workingDirectory = workingDirectoryAspect->workingDirectory(macroExpander()).toString();
    if (auto environmentAspect = aspect<EnvironmentAspect>())
        r.environment = environmentAspect->environment();
    return r;
}

OutputFormatter *RunConfiguration::createOutputFormatter() const
{
    if (m_outputFormatterCreator)
        return m_outputFormatterCreator(project());
    return new OutputFormatter();
}


/*!
    \class ProjectExplorer::IRunConfigurationFactory

    \brief The IRunConfigurationFactory class restores run configurations from
    settings.

    The run configuration factory is used for restoring run configurations from
    settings and for creating new run configurations in the \gui {Run Settings}
    dialog.
    To restore run configurations, use the
    \c {bool canRestore(Target *parent, const QString &id)}
    and \c {RunConfiguration* create(Target *parent, const QString &id)}
    functions.

    To generate a list of creatable run configurations, use the
    \c {QStringList availableCreationIds(Target *parent)} and
    \c {QString displayNameForType(const QString&)} functions. To create a
    run configuration, use \c create().
*/

/*!
    \fn QStringList ProjectExplorer::IRunConfigurationFactory::availableCreationIds(Target *parent) const

    Shows the list of possible additions to a target. Returns a list of types.
*/

/*!
    \fn QString ProjectExplorer::IRunConfigurationFactory::displayNameForId(Core::Id id) const
    Translates the types to names to display to the user.
*/

static QList<RunConfigurationFactory *> g_runConfigurationFactories;

RunConfigurationFactory::RunConfigurationFactory()
{
    g_runConfigurationFactories.append(this);
}

RunConfigurationFactory::~RunConfigurationFactory()
{
    g_runConfigurationFactories.removeOne(this);
}

QString RunConfigurationFactory::decoratedTargetName(const QString &targetName, Target *target)
{
    QString displayName;
    if (!targetName.isEmpty())
        displayName = QFileInfo(targetName).completeBaseName();
    Core::Id devType = DeviceTypeKitAspect::deviceTypeId(target->kit());
    if (devType != Constants::DESKTOP_DEVICE_TYPE) {
        if (IDevice::ConstPtr dev = DeviceKitAspect::device(target->kit())) {
            if (displayName.isEmpty()) {
                //: Shown in Run configuration if no executable is given, %1 is device name
                displayName = RunConfiguration::tr("Run on %1").arg(dev->displayName());
            } else {
                //: Shown in Run configuration, Add menu: "name of runnable (on device name)"
                displayName = RunConfiguration::tr("%1 (on %2)").arg(displayName, dev->displayName());
            }
        }
    }
    return displayName;
}

QList<RunConfigurationCreationInfo>
RunConfigurationFactory::availableCreators(Target *parent) const
{
    const QList<BuildTargetInfo> buildTargets = parent->applicationTargets();
    const bool hasAnyQtcRunnable = Utils::anyOf(buildTargets,
                                            Utils::equal(&BuildTargetInfo::isQtcRunnable, true));
    return Utils::transform(buildTargets, [&](const BuildTargetInfo &ti) {
        QString displayName = ti.displayName;
        if (displayName.isEmpty())
            displayName = decoratedTargetName(ti.buildKey, parent);
        else if (m_decorateDisplayNames)
            displayName = decoratedTargetName(displayName, parent);
        RunConfigurationCreationInfo rci;
        rci.factory = this;
        rci.id = m_runConfigBaseId;
        rci.buildKey = ti.buildKey;
        rci.projectFilePath = ti.projectFilePath;
        rci.displayName = displayName;
        rci.displayNameUniquifier = ti.displayNameUniquifier;
        rci.creationMode = ti.isQtcRunnable || !hasAnyQtcRunnable
                ? RunConfigurationCreationInfo::AlwaysCreate
                : RunConfigurationCreationInfo::ManualCreationOnly;
        rci.useTerminal = ti.usesTerminal;
        rci.buildKey = ti.buildKey;
        return rci;
    });
}

/*!
    Adds a list of device types for which this RunConfigurationFactory
    can create RunConfigurations.

    If this function is never called for a RunConfiguarionFactory,
    the factory will create RunConfigurations for all device types.
*/

void RunConfigurationFactory::addSupportedTargetDeviceType(Core::Id id)
{
    m_supportedTargetDeviceTypes.append(id);
}

void RunConfigurationFactory::setDecorateDisplayNames(bool on)
{
    m_decorateDisplayNames = on;
}

void RunConfigurationFactory::addSupportedProjectType(Core::Id id)
{
    m_supportedProjectTypes.append(id);
}

bool RunConfigurationFactory::canHandle(Target *target) const
{
    const Project *project = target->project();
    Kit *kit = target->kit();

    if (containsType(target->project()->projectIssues(kit), Task::TaskType::Error))
        return false;

    if (!m_supportedProjectTypes.isEmpty())
        if (!m_supportedProjectTypes.contains(project->id()))
            return false;

    if (!m_supportedTargetDeviceTypes.isEmpty())
        if (!m_supportedTargetDeviceTypes.contains(
                    DeviceTypeKitAspect::deviceTypeId(kit)))
            return false;

    return true;
}

RunConfiguration *RunConfigurationCreationInfo::create(Target *target) const
{
    QTC_ASSERT(factory->canHandle(target), return nullptr);
    QTC_ASSERT(id == factory->runConfigurationBaseId(), return nullptr);
    QTC_ASSERT(factory->m_creator, return nullptr);

    RunConfiguration *rc = factory->m_creator(target);
    if (!rc)
        return nullptr;

    rc->acquaintAspects();
    rc->m_buildKey = buildKey;
    rc->doAdditionalSetup(*this);
    rc->setDisplayName(displayName);

    return rc;
}

RunConfiguration *RunConfigurationFactory::restore(Target *parent, const QVariantMap &map)
{
    for (RunConfigurationFactory *factory : g_runConfigurationFactories) {
        if (factory->canHandle(parent)) {
            const Core::Id id = idFromMap(map);
            if (id.name().startsWith(factory->m_runConfigBaseId.name())) {
                QTC_ASSERT(factory->m_creator, continue);
                RunConfiguration *rc = factory->m_creator(parent);
                rc->acquaintAspects();
                if (rc->fromMap(map))
                    return rc;
                delete rc;
                return nullptr;
            }
        }
    }
    return nullptr;
}

RunConfiguration *RunConfigurationFactory::clone(Target *parent, RunConfiguration *source)
{
    return restore(parent, source->toMap());
}

const QList<RunConfigurationCreationInfo> RunConfigurationFactory::creatorsForTarget(Target *parent)
{
    QList<RunConfigurationCreationInfo> items;
    for (RunConfigurationFactory *factory : g_runConfigurationFactories) {
        if (factory->canHandle(parent))
            items.append(factory->availableCreators(parent));
    }
    QHash<QString, QList<RunConfigurationCreationInfo *>> itemsPerDisplayName;
    for (RunConfigurationCreationInfo &item : items)
        itemsPerDisplayName[item.displayName] << &item;
    for (auto it = itemsPerDisplayName.cbegin(); it != itemsPerDisplayName.cend(); ++it) {
        if (it.value().size() == 1)
            continue;
        for (RunConfigurationCreationInfo * const rci : it.value())
            rci->displayName += rci->displayNameUniquifier;
    }
    return items;
}

FixedRunConfigurationFactory::FixedRunConfigurationFactory(const QString &displayName,
                                                           bool addDeviceName) :
    m_fixedBuildTarget(displayName),
    m_decorateTargetName(addDeviceName)
{ }

QList<RunConfigurationCreationInfo>
FixedRunConfigurationFactory::availableCreators(Target *parent) const
{
    QString displayName = m_decorateTargetName ? decoratedTargetName(m_fixedBuildTarget, parent)
                                               : m_fixedBuildTarget;
    RunConfigurationCreationInfo rci;
    rci.factory = this;
    rci.id = runConfigurationBaseId();
    rci.displayName = displayName;
    return {rci};
}

} // namespace ProjectExplorer
