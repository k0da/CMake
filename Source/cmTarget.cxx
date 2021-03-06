/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#include "cmTarget.h"
#include "cmake.h"
#include "cmMakefile.h"
#include "cmSourceFile.h"
#include "cmLocalGenerator.h"
#include "cmGlobalGenerator.h"
#include "cmComputeLinkInformation.h"
#include "cmListFileCache.h"
#include "cmGeneratorExpression.h"
#include "cmGeneratorExpressionDAGChecker.h"
#include <cmsys/RegularExpression.hxx>
#include <map>
#include <set>
#include <stdlib.h> // required for atof
#include <assert.h>
#include <errno.h>

const char* cmTarget::GetTargetTypeName(TargetType targetType)
{
  switch( targetType )
    {
      case cmTarget::STATIC_LIBRARY:
        return "STATIC_LIBRARY";
      case cmTarget::MODULE_LIBRARY:
        return "MODULE_LIBRARY";
      case cmTarget::SHARED_LIBRARY:
        return "SHARED_LIBRARY";
      case cmTarget::OBJECT_LIBRARY:
        return "OBJECT_LIBRARY";
      case cmTarget::EXECUTABLE:
        return "EXECUTABLE";
      case cmTarget::UTILITY:
        return "UTILITY";
      case cmTarget::GLOBAL_TARGET:
        return "GLOBAL_TARGET";
      case cmTarget::INTERFACE_LIBRARY:
        return "INTERFACE_LIBRARY";
      case cmTarget::UNKNOWN_LIBRARY:
        return "UNKNOWN_LIBRARY";
    }
  assert(0 && "Unexpected target type");
  return 0;
}

//----------------------------------------------------------------------------
struct cmTarget::OutputInfo
{
  std::string OutDir;
  std::string ImpDir;
  std::string PdbDir;
};

//----------------------------------------------------------------------------
struct cmTarget::ImportInfo
{
  ImportInfo(): NoSOName(false), Multiplicity(0) {}
  bool NoSOName;
  int Multiplicity;
  std::string Location;
  std::string SOName;
  std::string ImportLibrary;
  std::string Languages;
  std::string Libraries;
  std::string LibrariesProp;
  std::string SharedDeps;
};

//----------------------------------------------------------------------------
struct cmTarget::CompileInfo
{
  std::string CompilePdbDir;
};

struct TargetConfigPair : public std::pair<cmTarget const* , std::string> {
  TargetConfigPair(cmTarget const* tgt, const std::string &config)
    : std::pair<cmTarget const* , std::string>(tgt, config) {}
};

//----------------------------------------------------------------------------
class cmTargetInternals
{
public:
  cmTargetInternals()
    : Backtrace(NULL)
    {
    this->PolicyWarnedCMP0022 = false;
    this->UtilityItemsDone = false;
    }
  cmTargetInternals(cmTargetInternals const&)
    : Backtrace(NULL)
    {
    this->PolicyWarnedCMP0022 = false;
    this->UtilityItemsDone = false;
    }
  ~cmTargetInternals();

  // The backtrace when the target was created.
  cmListFileBacktrace Backtrace;

  // Cache link interface computation from each configuration.
  struct OptionalLinkInterface: public cmTarget::LinkInterface
  {
    OptionalLinkInterface():
      Exists(false), Complete(false), ExplicitLibraries(0) {}
    bool Exists;
    bool Complete;
    const char* ExplicitLibraries;
  };
  void ComputeLinkInterface(cmTarget const* thisTarget,
                            const std::string& config,
                            OptionalLinkInterface& iface,
                            cmTarget const* head,
                            const char *explicitLibraries) const;

  typedef std::map<TargetConfigPair, OptionalLinkInterface>
                                                          LinkInterfaceMapType;
  LinkInterfaceMapType LinkInterfaceMap;
  LinkInterfaceMapType LinkInterfaceUsageRequirementsOnlyMap;
  bool PolicyWarnedCMP0022;

  typedef std::map<TargetConfigPair, cmTarget::LinkInterface>
                                                    ImportLinkInterfaceMapType;
  ImportLinkInterfaceMapType ImportLinkInterfaceMap;
  ImportLinkInterfaceMapType ImportLinkInterfaceUsageRequirementsOnlyMap;

  typedef std::map<std::string, cmTarget::OutputInfo> OutputInfoMapType;
  OutputInfoMapType OutputInfoMap;

  typedef std::map<std::string, cmTarget::ImportInfo> ImportInfoMapType;
  ImportInfoMapType ImportInfoMap;

  typedef std::map<std::string, cmTarget::CompileInfo> CompileInfoMapType;
  CompileInfoMapType CompileInfoMap;

  // Cache link implementation computation from each configuration.
  struct OptionalLinkImplementation: public cmTarget::LinkImplementation
  {
    OptionalLinkImplementation():
      LibrariesDone(false), LanguagesDone(false) {}
    bool LibrariesDone;
    bool LanguagesDone;
  };
  typedef std::map<TargetConfigPair,
                   OptionalLinkImplementation> LinkImplMapType;
  LinkImplMapType LinkImplMap;

  typedef std::map<std::string, cmTarget::LinkClosure> LinkClosureMapType;
  LinkClosureMapType LinkClosureMap;

  typedef std::map<TargetConfigPair, std::vector<cmSourceFile*> >
                                                          SourceFilesMapType;
  SourceFilesMapType SourceFilesMap;

  std::set<cmLinkItem> UtilityItems;
  bool UtilityItemsDone;

  struct TargetPropertyEntry {
    TargetPropertyEntry(cmsys::auto_ptr<cmCompiledGeneratorExpression> cge,
      const std::string &targetName = std::string())
      : ge(cge), TargetName(targetName)
    {}
    const cmsys::auto_ptr<cmCompiledGeneratorExpression> ge;
    std::vector<std::string> CachedEntries;
    const std::string TargetName;
  };
  std::vector<TargetPropertyEntry*> IncludeDirectoriesEntries;
  std::vector<TargetPropertyEntry*> CompileOptionsEntries;
  std::vector<TargetPropertyEntry*> CompileFeaturesEntries;
  std::vector<TargetPropertyEntry*> CompileDefinitionsEntries;
  std::vector<TargetPropertyEntry*> SourceEntries;
  std::vector<cmValueWithOrigin> LinkImplementationPropertyEntries;

  std::map<std::string, std::vector<TargetPropertyEntry*> >
                        CachedLinkInterfaceIncludeDirectoriesEntries;
  std::map<std::string, std::vector<TargetPropertyEntry*> >
                        CachedLinkInterfaceCompileOptionsEntries;
  std::map<std::string, std::vector<TargetPropertyEntry*> >
                        CachedLinkInterfaceCompileDefinitionsEntries;
  std::map<std::string, std::vector<TargetPropertyEntry*> >
                        CachedLinkInterfaceSourcesEntries;
  std::map<std::string, std::vector<TargetPropertyEntry*> >
                        CachedLinkInterfaceCompileFeaturesEntries;
  std::map<std::string, std::vector<cmTarget const*> >
                        CachedLinkImplementationClosure;

  std::map<std::string, bool> CacheLinkInterfaceIncludeDirectoriesDone;
  std::map<std::string, bool> CacheLinkInterfaceCompileDefinitionsDone;
  std::map<std::string, bool> CacheLinkInterfaceCompileOptionsDone;
  std::map<std::string, bool> CacheLinkInterfaceSourcesDone;
  std::map<std::string, bool> CacheLinkInterfaceCompileFeaturesDone;
  std::map<std::string, bool> CacheLinkImplementationClosureDone;
};

//----------------------------------------------------------------------------
void deleteAndClear(
      std::vector<cmTargetInternals::TargetPropertyEntry*> &entries)
{
  for (std::vector<cmTargetInternals::TargetPropertyEntry*>::const_iterator
      it = entries.begin(),
      end = entries.end();
      it != end; ++it)
    {
      delete *it;
    }
  entries.clear();
}

//----------------------------------------------------------------------------
void deleteAndClear(
  std::map<std::string,
          std::vector<cmTargetInternals::TargetPropertyEntry*> > &entries)
{
  for (std::map<std::string,
          std::vector<cmTargetInternals::TargetPropertyEntry*> >::iterator
        it = entries.begin(), end = entries.end(); it != end; ++it)
    {
    deleteAndClear(it->second);
    }
}

//----------------------------------------------------------------------------
cmTargetInternals::~cmTargetInternals()
{
  deleteAndClear(this->CachedLinkInterfaceIncludeDirectoriesEntries);
  deleteAndClear(this->CachedLinkInterfaceCompileOptionsEntries);
  deleteAndClear(this->CachedLinkInterfaceCompileFeaturesEntries);
  deleteAndClear(this->CachedLinkInterfaceCompileDefinitionsEntries);
  deleteAndClear(this->CachedLinkInterfaceSourcesEntries);
}

//----------------------------------------------------------------------------
cmTarget::cmTarget()
{
#define INITIALIZE_TARGET_POLICY_MEMBER(POLICY) \
  this->PolicyStatus ## POLICY = cmPolicies::WARN;

  CM_FOR_EACH_TARGET_POLICY(INITIALIZE_TARGET_POLICY_MEMBER)

#undef INITIALIZE_TARGET_POLICY_MEMBER

  this->Makefile = 0;
  this->LinkLibrariesAnalyzed = false;
  this->HaveInstallRule = false;
  this->DLLPlatform = false;
  this->IsApple = false;
  this->IsImportedTarget = false;
  this->BuildInterfaceIncludesAppended = false;
  this->DebugIncludesDone = false;
  this->DebugCompileOptionsDone = false;
  this->DebugCompileFeaturesDone = false;
  this->DebugCompileDefinitionsDone = false;
  this->DebugSourcesDone = false;
  this->LinkImplementationLanguageIsContextDependent = true;
}

//----------------------------------------------------------------------------
void cmTarget::DefineProperties(cmake *cm)
{
  cm->DefineProperty
    ("RULE_LAUNCH_COMPILE", cmProperty::TARGET,
     "", "", true);
  cm->DefineProperty
    ("RULE_LAUNCH_LINK", cmProperty::TARGET,
     "", "", true);
  cm->DefineProperty
    ("RULE_LAUNCH_CUSTOM", cmProperty::TARGET,
     "", "", true);
}

void cmTarget::SetType(TargetType type, const std::string& name)
{
  this->Name = name;
  // only add dependency information for library targets
  this->TargetTypeValue = type;
  if(this->TargetTypeValue >= STATIC_LIBRARY
     && this->TargetTypeValue <= MODULE_LIBRARY)
    {
    this->RecordDependencies = true;
    }
  else
    {
    this->RecordDependencies = false;
    }
}

//----------------------------------------------------------------------------
void cmTarget::SetMakefile(cmMakefile* mf)
{
  // Set our makefile.
  this->Makefile = mf;

  // set the cmake instance of the properties
  this->Properties.SetCMakeInstance(mf->GetCMakeInstance());

  // Check whether this is a DLL platform.
  this->DLLPlatform = (this->Makefile->IsOn("WIN32") ||
                       this->Makefile->IsOn("CYGWIN") ||
                       this->Makefile->IsOn("MINGW"));

  // Check whether we are targeting an Apple platform.
  this->IsApple = this->Makefile->IsOn("APPLE");

  // Setup default property values.
  if (this->GetType() != INTERFACE_LIBRARY)
    {
    this->SetPropertyDefault("INSTALL_NAME_DIR", 0);
    this->SetPropertyDefault("INSTALL_RPATH", "");
    this->SetPropertyDefault("INSTALL_RPATH_USE_LINK_PATH", "OFF");
    this->SetPropertyDefault("SKIP_BUILD_RPATH", "OFF");
    this->SetPropertyDefault("BUILD_WITH_INSTALL_RPATH", "OFF");
    this->SetPropertyDefault("ARCHIVE_OUTPUT_DIRECTORY", 0);
    this->SetPropertyDefault("LIBRARY_OUTPUT_DIRECTORY", 0);
    this->SetPropertyDefault("RUNTIME_OUTPUT_DIRECTORY", 0);
    this->SetPropertyDefault("PDB_OUTPUT_DIRECTORY", 0);
    this->SetPropertyDefault("COMPILE_PDB_OUTPUT_DIRECTORY", 0);
    this->SetPropertyDefault("Fortran_FORMAT", 0);
    this->SetPropertyDefault("Fortran_MODULE_DIRECTORY", 0);
    this->SetPropertyDefault("GNUtoMS", 0);
    this->SetPropertyDefault("OSX_ARCHITECTURES", 0);
    this->SetPropertyDefault("AUTOMOC", 0);
    this->SetPropertyDefault("AUTOUIC", 0);
    this->SetPropertyDefault("AUTORCC", 0);
    this->SetPropertyDefault("AUTOMOC_MOC_OPTIONS", 0);
    this->SetPropertyDefault("AUTOUIC_OPTIONS", 0);
    this->SetPropertyDefault("AUTORCC_OPTIONS", 0);
    this->SetPropertyDefault("LINK_DEPENDS_NO_SHARED", 0);
    this->SetPropertyDefault("LINK_INTERFACE_LIBRARIES", 0);
    this->SetPropertyDefault("WIN32_EXECUTABLE", 0);
    this->SetPropertyDefault("MACOSX_BUNDLE", 0);
    this->SetPropertyDefault("MACOSX_RPATH", 0);
    this->SetPropertyDefault("NO_SYSTEM_FROM_IMPORTED", 0);
    this->SetPropertyDefault("C_STANDARD", 0);
    this->SetPropertyDefault("C_STANDARD_REQUIRED", 0);
    this->SetPropertyDefault("C_EXTENSIONS", 0);
    this->SetPropertyDefault("CXX_STANDARD", 0);
    this->SetPropertyDefault("CXX_STANDARD_REQUIRED", 0);
    this->SetPropertyDefault("CXX_EXTENSIONS", 0);
    }

  // Collect the set of configuration types.
  std::vector<std::string> configNames;
  mf->GetConfigurations(configNames);

  // Setup per-configuration property default values.
  const char* configProps[] = {
    "ARCHIVE_OUTPUT_DIRECTORY_",
    "LIBRARY_OUTPUT_DIRECTORY_",
    "RUNTIME_OUTPUT_DIRECTORY_",
    "PDB_OUTPUT_DIRECTORY_",
    "COMPILE_PDB_OUTPUT_DIRECTORY_",
    "MAP_IMPORTED_CONFIG_",
    0};
  for(std::vector<std::string>::iterator ci = configNames.begin();
      ci != configNames.end(); ++ci)
    {
    std::string configUpper = cmSystemTools::UpperCase(*ci);
    for(const char** p = configProps; *p; ++p)
      {
      if (this->TargetTypeValue == INTERFACE_LIBRARY
          && strcmp(*p, "MAP_IMPORTED_CONFIG_") != 0)
        {
        continue;
        }
      std::string property = *p;
      property += configUpper;
      this->SetPropertyDefault(property, 0);
      }

    // Initialize per-configuration name postfix property from the
    // variable only for non-executable targets.  This preserves
    // compatibility with previous CMake versions in which executables
    // did not support this variable.  Projects may still specify the
    // property directly.
    if(this->TargetTypeValue != cmTarget::EXECUTABLE
        && this->TargetTypeValue != cmTarget::INTERFACE_LIBRARY)
      {
      std::string property = cmSystemTools::UpperCase(*ci);
      property += "_POSTFIX";
      this->SetPropertyDefault(property, 0);
      }
    }

  // Save the backtrace of target construction.
  this->Internal->Backtrace = this->Makefile->GetBacktrace();

  if (!this->IsImported())
    {
    // Initialize the INCLUDE_DIRECTORIES property based on the current value
    // of the same directory property:
    const std::vector<cmValueWithOrigin> parentIncludes =
                                this->Makefile->GetIncludeDirectoriesEntries();

    for (std::vector<cmValueWithOrigin>::const_iterator it
                = parentIncludes.begin(); it != parentIncludes.end(); ++it)
      {
      this->InsertInclude(*it);
      }
    const std::set<std::string> parentSystemIncludes =
                                this->Makefile->GetSystemIncludeDirectories();

    for (std::set<std::string>::const_iterator it
          = parentSystemIncludes.begin();
          it != parentSystemIncludes.end(); ++it)
      {
      this->SystemIncludeDirectories.insert(*it);
      }

    const std::vector<cmValueWithOrigin> parentOptions =
                                this->Makefile->GetCompileOptionsEntries();

    for (std::vector<cmValueWithOrigin>::const_iterator it
                = parentOptions.begin(); it != parentOptions.end(); ++it)
      {
      this->InsertCompileOption(*it);
      }
    }

  if (this->GetType() != INTERFACE_LIBRARY)
    {
    this->SetPropertyDefault("C_VISIBILITY_PRESET", 0);
    this->SetPropertyDefault("CXX_VISIBILITY_PRESET", 0);
    this->SetPropertyDefault("VISIBILITY_INLINES_HIDDEN", 0);
    }

  if(this->TargetTypeValue == cmTarget::SHARED_LIBRARY
      || this->TargetTypeValue == cmTarget::MODULE_LIBRARY)
    {
    this->SetProperty("POSITION_INDEPENDENT_CODE", "True");
    }
  if (this->GetType() != INTERFACE_LIBRARY)
    {
    this->SetPropertyDefault("POSITION_INDEPENDENT_CODE", 0);
    }

  // Record current policies for later use.
#define CAPTURE_TARGET_POLICY(POLICY) \
  this->PolicyStatus ## POLICY = \
    this->Makefile->GetPolicyStatus(cmPolicies::POLICY);

  CM_FOR_EACH_TARGET_POLICY(CAPTURE_TARGET_POLICY)

#undef CAPTURE_TARGET_POLICY

  if (this->TargetTypeValue == INTERFACE_LIBRARY)
    {
    // This policy is checked in a few conditions. The properties relevant
    // to the policy are always ignored for INTERFACE_LIBRARY targets,
    // so ensure that the conditions don't lead to nonsense.
    this->PolicyStatusCMP0022 = cmPolicies::NEW;
    }

  this->SetPropertyDefault("JOB_POOL_COMPILE", 0);
  this->SetPropertyDefault("JOB_POOL_LINK", 0);
}

//----------------------------------------------------------------------------
void cmTarget::AddUtility(const std::string& u, cmMakefile *makefile)
{
  if(this->Utilities.insert(u).second && makefile)
    {
    UtilityBacktraces.insert(std::make_pair(u, makefile->GetBacktrace()));
    }
}

//----------------------------------------------------------------------------
cmListFileBacktrace const* cmTarget::GetUtilityBacktrace(
    const std::string& u) const
{
  std::map<std::string, cmListFileBacktrace>::const_iterator i =
    this->UtilityBacktraces.find(u);
  if(i == this->UtilityBacktraces.end()) return 0;

  return &i->second;
}

//----------------------------------------------------------------------------
std::set<cmLinkItem> const& cmTarget::GetUtilityItems() const
{
  if(!this->Internal->UtilityItemsDone)
    {
    this->Internal->UtilityItemsDone = true;
    for(std::set<std::string>::const_iterator i = this->Utilities.begin();
        i != this->Utilities.end(); ++i)
      {
      this->Internal->UtilityItems.insert(
        cmLinkItem(*i, this->Makefile->FindTargetToUse(*i)));
      }
    }
  return this->Internal->UtilityItems;
}

//----------------------------------------------------------------------------
void cmTarget::FinishConfigure()
{
  // Erase any cached link information that might have been comptued
  // on-demand during the configuration.  This ensures that build
  // system generation uses up-to-date information even if other cache
  // invalidation code in this source file is buggy.
  this->ClearLinkMaps();

  // Do old-style link dependency analysis.
  this->AnalyzeLibDependencies(*this->Makefile);
}

//----------------------------------------------------------------------------
void cmTarget::ClearLinkMaps()
{
  this->LinkImplementationLanguageIsContextDependent = true;
  this->Internal->LinkImplMap.clear();
  this->Internal->LinkInterfaceMap.clear();
  this->Internal->LinkInterfaceUsageRequirementsOnlyMap.clear();
  this->Internal->ImportLinkInterfaceMap.clear();
  this->Internal->ImportLinkInterfaceUsageRequirementsOnlyMap.clear();
  this->Internal->LinkClosureMap.clear();
  for (cmTargetLinkInformationMap::const_iterator it
      = this->LinkInformation.begin();
      it != this->LinkInformation.end(); ++it)
    {
    delete it->second;
    }
  this->LinkInformation.clear();
}

//----------------------------------------------------------------------------
cmListFileBacktrace const& cmTarget::GetBacktrace() const
{
  return this->Internal->Backtrace;
}

//----------------------------------------------------------------------------
std::string cmTarget::GetSupportDirectory() const
{
  std::string dir = this->Makefile->GetCurrentOutputDirectory();
  dir += cmake::GetCMakeFilesDirectory();
  dir += "/";
  dir += this->Name;
#if defined(__VMS)
  dir += "_dir";
#else
  dir += ".dir";
#endif
  return dir;
}

//----------------------------------------------------------------------------
bool cmTarget::IsExecutableWithExports() const
{
  return (this->GetType() == cmTarget::EXECUTABLE &&
          this->GetPropertyAsBool("ENABLE_EXPORTS"));
}

//----------------------------------------------------------------------------
bool cmTarget::IsLinkable() const
{
  return (this->GetType() == cmTarget::STATIC_LIBRARY ||
          this->GetType() == cmTarget::SHARED_LIBRARY ||
          this->GetType() == cmTarget::MODULE_LIBRARY ||
          this->GetType() == cmTarget::UNKNOWN_LIBRARY ||
          this->GetType() == cmTarget::INTERFACE_LIBRARY ||
          this->IsExecutableWithExports());
}

//----------------------------------------------------------------------------
bool cmTarget::HasImportLibrary() const
{
  return (this->DLLPlatform &&
          (this->GetType() == cmTarget::SHARED_LIBRARY ||
           this->IsExecutableWithExports()));
}

//----------------------------------------------------------------------------
bool cmTarget::IsFrameworkOnApple() const
{
  return (this->GetType() == cmTarget::SHARED_LIBRARY &&
          this->Makefile->IsOn("APPLE") &&
          this->GetPropertyAsBool("FRAMEWORK"));
}

//----------------------------------------------------------------------------
bool cmTarget::IsAppBundleOnApple() const
{
  return (this->GetType() == cmTarget::EXECUTABLE &&
          this->Makefile->IsOn("APPLE") &&
          this->GetPropertyAsBool("MACOSX_BUNDLE"));
}

//----------------------------------------------------------------------------
bool cmTarget::IsCFBundleOnApple() const
{
  return (this->GetType() == cmTarget::MODULE_LIBRARY &&
          this->Makefile->IsOn("APPLE") &&
          this->GetPropertyAsBool("BUNDLE"));
}

//----------------------------------------------------------------------------
bool cmTarget::IsBundleOnApple() const
{
  return this->IsFrameworkOnApple() || this->IsAppBundleOnApple() ||
         this->IsCFBundleOnApple();
}

//----------------------------------------------------------------------------
static bool processSources(cmTarget const* tgt,
      const std::vector<cmTargetInternals::TargetPropertyEntry*> &entries,
      std::vector<std::string> &srcs,
      std::set<std::string> &uniqueSrcs,
      cmGeneratorExpressionDAGChecker *dagChecker,
      cmTarget const* head,
      std::string const& config, bool debugSources)
{
  cmMakefile *mf = tgt->GetMakefile();

  bool contextDependent = false;

  for (std::vector<cmTargetInternals::TargetPropertyEntry*>::const_iterator
      it = entries.begin(), end = entries.end(); it != end; ++it)
    {
    bool cacheSources = false;
    std::vector<std::string> entrySources = (*it)->CachedEntries;
    if(entrySources.empty())
      {
      cmSystemTools::ExpandListArgument((*it)->ge->Evaluate(mf,
                                                config,
                                                false,
                                                head ? head : tgt,
                                                tgt,
                                                dagChecker),
                                      entrySources);

      if ((*it)->ge->GetHadContextSensitiveCondition())
        {
        contextDependent = true;
        }
      else if (mf->IsGeneratingBuildSystem())
        {
        cacheSources = true;
        }

      for(std::vector<std::string>::iterator i = entrySources.begin();
          i != entrySources.end(); ++i)
        {
        std::string& src = *i;

        cmSourceFile* sf = mf->GetOrCreateSource(src);
        std::string e;
        src = sf->GetFullPath(&e);
        if(src.empty())
          {
          if(!e.empty())
            {
            cmake* cm = mf->GetCMakeInstance();
            cm->IssueMessage(cmake::FATAL_ERROR, e,
                            tgt->GetBacktrace());
            }
          return contextDependent;
          }
        }
      if (cacheSources)
        {
        (*it)->CachedEntries = entrySources;
        }
      }
    std::string usedSources;
    for(std::vector<std::string>::iterator
          li = entrySources.begin(); li != entrySources.end(); ++li)
      {
      std::string src = *li;

      if(uniqueSrcs.insert(src).second)
        {
        srcs.push_back(src);
        if (debugSources)
          {
          usedSources += " * " + src + "\n";
          }
        }
      }
    if (!usedSources.empty())
      {
      mf->GetCMakeInstance()->IssueMessage(cmake::LOG,
                            std::string("Used sources for target ")
                            + tgt->GetName() + ":\n"
                            + usedSources, (*it)->ge->GetBacktrace());
      }
    }
  return contextDependent;
}

//----------------------------------------------------------------------------
void cmTarget::GetSourceFiles(std::vector<std::string> &files,
                              const std::string& config,
                              cmTarget const* head) const
{
  assert(this->GetType() != INTERFACE_LIBRARY);

  if (this->Makefile->GetGeneratorTargets().empty())
    {
    // At configure-time, this method can be called as part of getting the
    // LOCATION property or to export() a file to be include()d.  However
    // there is no cmGeneratorTarget at configure-time, so search the SOURCES
    // for TARGET_OBJECTS instead for backwards compatibility with OLD
    // behavior of CMP0024 and CMP0026 only.

    typedef cmTargetInternals::TargetPropertyEntry
                                TargetPropertyEntry;
    for(std::vector<TargetPropertyEntry*>::const_iterator
          i = this->Internal->SourceEntries.begin();
        i != this->Internal->SourceEntries.end(); ++i)
      {
      std::string entry = (*i)->ge->GetInput();

      std::vector<std::string> items;
      cmSystemTools::ExpandListArgument(entry, items);
      for (std::vector<std::string>::const_iterator
          li = items.begin(); li != items.end(); ++li)
        {
        if(cmHasLiteralPrefix(*li, "$<TARGET_OBJECTS:") &&
            (*li)[li->size() - 1] == '>')
          {
          continue;
          }
        files.push_back(*li);
        }
      }
    return;
    }

  std::vector<std::string> debugProperties;
  const char *debugProp =
              this->Makefile->GetDefinition("CMAKE_DEBUG_TARGET_PROPERTIES");
  if (debugProp)
    {
    cmSystemTools::ExpandListArgument(debugProp, debugProperties);
    }

  bool debugSources = !this->DebugSourcesDone
                    && std::find(debugProperties.begin(),
                                 debugProperties.end(),
                                 "SOURCES")
                        != debugProperties.end();

  if (this->Makefile->IsGeneratingBuildSystem())
    {
    this->DebugSourcesDone = true;
    }

  cmGeneratorExpressionDAGChecker dagChecker(this->GetName(),
                                             "SOURCES", 0, 0);

  std::set<std::string> uniqueSrcs;
  bool contextDependentDirectSources = processSources(this,
                 this->Internal->SourceEntries,
                 files,
                 uniqueSrcs,
                 &dagChecker,
                 head,
                 config,
                 debugSources);

  if (!this->Internal->CacheLinkInterfaceSourcesDone[config])
    {
    for (std::vector<cmValueWithOrigin>::const_iterator
        it = this->Internal->LinkImplementationPropertyEntries.begin(),
        end = this->Internal->LinkImplementationPropertyEntries.end();
        it != end; ++it)
      {
      if (!cmGeneratorExpression::IsValidTargetName(it->Value)
          && cmGeneratorExpression::Find(it->Value) == std::string::npos)
        {
        continue;
        }
      {
      cmGeneratorExpression ge;
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge =
                                                        ge.Parse(it->Value);
      std::string targetResult = cge->Evaluate(this->Makefile, config,
                                        false, this, 0, &dagChecker);
      if (!this->Makefile->FindTargetToUse(targetResult))
        {
        continue;
        }
      }
      std::string sourceGenex = "$<TARGET_PROPERTY:" +
                              it->Value + ",INTERFACE_SOURCES>";
      if (cmGeneratorExpression::Find(it->Value) != std::string::npos)
        {
        // Because it->Value is a generator expression, ensure that it
        // evaluates to the non-empty string before being used in the
        // TARGET_PROPERTY expression.
        sourceGenex = "$<$<BOOL:" + it->Value + ">:" + sourceGenex + ">";
        }
      cmGeneratorExpression ge(&it->Backtrace);
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(
                                                                sourceGenex);

      this->Internal
        ->CachedLinkInterfaceSourcesEntries[config].push_back(
                        new cmTargetInternals::TargetPropertyEntry(cge,
                                                              it->Value));
      }
    }

    std::vector<std::string>::size_type numFilesBefore = files.size();
    bool contextDependentInterfaceSources = processSources(this,
    this->Internal->CachedLinkInterfaceSourcesEntries[config],
                            files,
                            uniqueSrcs,
                            &dagChecker,
                            head,
                            config,
                            debugSources);

  if (!contextDependentDirectSources
      && !(contextDependentInterfaceSources && numFilesBefore < files.size()))
    {
    this->LinkImplementationLanguageIsContextDependent = false;
    }

  if (!this->Makefile->IsGeneratingBuildSystem())
    {
    deleteAndClear(this->Internal->CachedLinkInterfaceSourcesEntries);
    }
  else
    {
    this->Internal->CacheLinkInterfaceSourcesDone[config] = true;
    }
}

//----------------------------------------------------------------------------
bool
cmTarget::GetConfigCommonSourceFiles(std::vector<cmSourceFile*>& files) const
{
  std::vector<std::string> configs;
  this->Makefile->GetConfigurations(configs);
  if (configs.empty())
    {
    configs.push_back("");
    }

  std::vector<std::string>::const_iterator it = configs.begin();
  const std::string& firstConfig = *it;
  this->GetSourceFiles(files, firstConfig);

  for ( ; it != configs.end(); ++it)
    {
    std::vector<cmSourceFile*> configFiles;
    this->GetSourceFiles(configFiles, *it);
    if (configFiles != files)
      {
      std::string firstConfigFiles;
      const char* sep = "";
      for (std::vector<cmSourceFile*>::const_iterator fi = files.begin();
           fi != files.end(); ++fi)
        {
        firstConfigFiles += sep;
        firstConfigFiles += (*fi)->GetFullPath();
        sep = "\n  ";
        }

      std::string thisConfigFiles;
      sep = "";
      for (std::vector<cmSourceFile*>::const_iterator fi = configFiles.begin();
           fi != configFiles.end(); ++fi)
        {
        thisConfigFiles += sep;
        thisConfigFiles += (*fi)->GetFullPath();
        sep = "\n  ";
        }
      cmOStringStream e;
      e << "Target \"" << this->Name << "\" has source files which vary by "
        "configuration. This is not supported by the \""
        << this->Makefile->GetLocalGenerator()
                         ->GetGlobalGenerator()->GetName()
        << "\" generator.\n"
          "Config \"" << firstConfig << "\":\n"
          "  " << firstConfigFiles << "\n"
          "Config \"" << *it << "\":\n"
          "  " << thisConfigFiles << "\n";
      this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
      return false;
      }
    }
  return true;
}

//----------------------------------------------------------------------------
void cmTarget::GetSourceFiles(std::vector<cmSourceFile*> &files,
                              const std::string& config,
                              cmTarget const* head) const
{

  // Lookup any existing link implementation for this configuration.
  TargetConfigPair key(head, cmSystemTools::UpperCase(config));

  if(!this->LinkImplementationLanguageIsContextDependent)
    {
    files = this->Internal->SourceFilesMap.begin()->second;
    return;
    }

  cmTargetInternals::SourceFilesMapType::iterator
    it = this->Internal->SourceFilesMap.find(key);
  if(it != this->Internal->SourceFilesMap.end())
    {
    files = it->second;
    }
  else
    {
    std::vector<std::string> srcs;
    this->GetSourceFiles(srcs, config, head);

    std::set<cmSourceFile*> emitted;

    for(std::vector<std::string>::const_iterator i = srcs.begin();
        i != srcs.end(); ++i)
      {
      cmSourceFile* sf = this->Makefile->GetOrCreateSource(*i);
      if (emitted.insert(sf).second)
        {
        files.push_back(sf);
        }
      }
    this->Internal->SourceFilesMap[key] = files;
    }
}

//----------------------------------------------------------------------------
void cmTarget::AddTracedSources(std::vector<std::string> const& srcs)
{
  std::string srcFiles;
  const char* sep = "";
  for(std::vector<std::string>::const_iterator i = srcs.begin();
      i != srcs.end(); ++i)
    {
    std::string filename = *i;
    srcFiles += sep;
    srcFiles += filename;
    sep = ";";
    }
  if (!srcFiles.empty())
    {
    this->Internal->SourceFilesMap.clear();
    this->LinkImplementationLanguageIsContextDependent = true;
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(srcFiles);
    cge->SetEvaluateForBuildsystem(true);
    this->Internal->SourceEntries.push_back(
                          new cmTargetInternals::TargetPropertyEntry(cge));
    }
}

//----------------------------------------------------------------------------
void cmTarget::AddSources(std::vector<std::string> const& srcs)
{
  std::string srcFiles;
  const char* sep = "";
  for(std::vector<std::string>::const_iterator i = srcs.begin();
      i != srcs.end(); ++i)
    {
    std::string filename = *i;
    const char* src = filename.c_str();

    if(!(src[0] == '$' && src[1] == '<'))
      {
      filename = this->ProcessSourceItemCMP0049(filename);
      if (cmSystemTools::GetErrorOccuredFlag())
        {
        return;
        }
      this->Makefile->GetOrCreateSource(filename);
      }
    srcFiles += sep;
    srcFiles += filename;
    sep = ";";
    }
  if (!srcFiles.empty())
    {
    this->Internal->SourceFilesMap.clear();
    this->LinkImplementationLanguageIsContextDependent = true;
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(srcFiles);
    cge->SetEvaluateForBuildsystem(true);
    this->Internal->SourceEntries.push_back(
                          new cmTargetInternals::TargetPropertyEntry(cge));
    }
}

//----------------------------------------------------------------------------
std::string cmTarget::ProcessSourceItemCMP0049(const std::string& s)
{
  std::string src = s;

  // For backwards compatibility replace varibles in source names.
  // This should eventually be removed.
  this->Makefile->ExpandVariablesInString(src);
  if (src != s)
    {
    cmOStringStream e;
    bool noMessage = false;
    cmake::MessageType messageType = cmake::AUTHOR_WARNING;
    switch(this->Makefile->GetPolicyStatus(cmPolicies::CMP0049))
      {
      case cmPolicies::WARN:
        e << (this->Makefile->GetPolicies()
              ->GetPolicyWarning(cmPolicies::CMP0049)) << "\n";
        break;
      case cmPolicies::OLD:
        noMessage = true;
        break;
      case cmPolicies::REQUIRED_ALWAYS:
      case cmPolicies::REQUIRED_IF_USED:
      case cmPolicies::NEW:
        messageType = cmake::FATAL_ERROR;
      }
    if (!noMessage)
      {
      e << "Legacy variable expansion in source file \""
        << s << "\" expanded to \"" << src << "\" in target \""
        << this->GetName() << "\".  This behavior will be removed in a "
        "future version of CMake.";
      this->Makefile->IssueMessage(messageType, e.str());
      if (messageType == cmake::FATAL_ERROR)
        {
        return "";
        }
      }
    }
  return src;
}

//----------------------------------------------------------------------------
cmSourceFile* cmTarget::AddSourceCMP0049(const std::string& s)
{
  std::string src = this->ProcessSourceItemCMP0049(s);

  if (cmSystemTools::GetErrorOccuredFlag())
    {
    return 0;
    }
  return this->AddSource(src);
}

//----------------------------------------------------------------------------
struct CreateLocation
{
  cmMakefile const* Makefile;

  CreateLocation(cmMakefile const* mf)
    : Makefile(mf)
  {

  }

  cmSourceFileLocation operator()(const std::string& filename)
  {
    return cmSourceFileLocation(this->Makefile, filename);
  }
};

//----------------------------------------------------------------------------
struct LocationMatcher
{
  const cmSourceFileLocation& Needle;

  LocationMatcher(const cmSourceFileLocation& needle)
    : Needle(needle)
  {

  }

  bool operator()(cmSourceFileLocation &loc)
  {
    return loc.Matches(this->Needle);
  }
};


//----------------------------------------------------------------------------
struct TargetPropertyEntryFinder
{
private:
  const cmSourceFileLocation& Needle;
public:
  TargetPropertyEntryFinder(const cmSourceFileLocation& needle)
    : Needle(needle)
  {

  }

  bool operator()(cmTargetInternals::TargetPropertyEntry* entry)
  {
    std::vector<std::string> files;
    cmSystemTools::ExpandListArgument(entry->ge->GetInput(), files);
    std::vector<cmSourceFileLocation> locations(files.size());
    std::transform(files.begin(), files.end(), locations.begin(),
                   CreateLocation(this->Needle.GetMakefile()));

    return std::find_if(locations.begin(), locations.end(),
        LocationMatcher(this->Needle)) != locations.end();
  }
};

//----------------------------------------------------------------------------
cmSourceFile* cmTarget::AddSource(const std::string& src)
{
  cmSourceFileLocation sfl(this->Makefile, src);
  if (std::find_if(this->Internal->SourceEntries.begin(),
                   this->Internal->SourceEntries.end(),
                   TargetPropertyEntryFinder(sfl))
                                      == this->Internal->SourceEntries.end())
    {
    this->Internal->SourceFilesMap.clear();
    this->LinkImplementationLanguageIsContextDependent = true;
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(src);
    cge->SetEvaluateForBuildsystem(true);
    this->Internal->SourceEntries.push_back(
                          new cmTargetInternals::TargetPropertyEntry(cge));
    }
  if (cmGeneratorExpression::Find(src) != std::string::npos)
    {
    return 0;
    }
  return this->Makefile->GetOrCreateSource(src);
}

//----------------------------------------------------------------------------
void cmTarget::MergeLinkLibraries( cmMakefile& mf,
                                   const std::string& selfname,
                                   const LinkLibraryVectorType& libs )
{
  // Only add on libraries we haven't added on before.
  // Assumption: the global link libraries could only grow, never shrink
  LinkLibraryVectorType::const_iterator i = libs.begin();
  i += this->PrevLinkedLibraries.size();
  for( ; i != libs.end(); ++i )
    {
    // This is equivalent to the target_link_libraries plain signature.
    this->AddLinkLibrary( mf, selfname, i->first, i->second );
    this->AppendProperty("INTERFACE_LINK_LIBRARIES",
      this->GetDebugGeneratorExpressions(i->first, i->second).c_str());
    }
  this->PrevLinkedLibraries = libs;
}

//----------------------------------------------------------------------------
void cmTarget::AddLinkDirectory(const std::string& d)
{
  // Make sure we don't add unnecessary search directories.
  if(this->LinkDirectoriesEmmitted.insert(d).second)
    {
    this->LinkDirectories.push_back(d);
    }
}

//----------------------------------------------------------------------------
const std::vector<std::string>& cmTarget::GetLinkDirectories() const
{
  return this->LinkDirectories;
}

//----------------------------------------------------------------------------
cmTarget::LinkLibraryType cmTarget::ComputeLinkType(
                                      const std::string& config) const
{
  // No configuration is always optimized.
  if(config.empty())
    {
    return cmTarget::OPTIMIZED;
    }

  // Get the list of configurations considered to be DEBUG.
  std::vector<std::string> const& debugConfigs =
    this->Makefile->GetCMakeInstance()->GetDebugConfigs();

  // Check if any entry in the list matches this configuration.
  std::string configUpper = cmSystemTools::UpperCase(config);
  for(std::vector<std::string>::const_iterator i = debugConfigs.begin();
      i != debugConfigs.end(); ++i)
    {
    if(*i == configUpper)
      {
      return cmTarget::DEBUG;
      }
    }

  // The current configuration is not a debug configuration.
  return cmTarget::OPTIMIZED;
}

//----------------------------------------------------------------------------
void cmTarget::ClearDependencyInformation( cmMakefile& mf,
                                           const std::string& target )
{
  // Clear the dependencies. The cache variable must exist iff we are
  // recording dependency information for this target.
  std::string depname = target;
  depname += "_LIB_DEPENDS";
  if (this->RecordDependencies)
    {
    mf.AddCacheDefinition(depname, "",
                          "Dependencies for target", cmCacheManager::STATIC);
    }
  else
    {
    if (mf.GetDefinition( depname ))
      {
      std::string message = "Target ";
      message += target;
      message += " has dependency information when it shouldn't.\n";
      message += "Your cache is probably stale. Please remove the entry\n  ";
      message += depname;
      message += "\nfrom the cache.";
      cmSystemTools::Error( message.c_str() );
      }
    }
}

//----------------------------------------------------------------------------
bool cmTarget::NameResolvesToFramework(const std::string& libname) const
{
  return this->Makefile->GetLocalGenerator()->GetGlobalGenerator()->
    NameResolvesToFramework(libname);
}

//----------------------------------------------------------------------------
std::string cmTarget::GetDebugGeneratorExpressions(const std::string &value,
                                  cmTarget::LinkLibraryType llt) const
{
  if (llt == GENERAL)
    {
    return value;
    }

  // Get the list of configurations considered to be DEBUG.
  std::vector<std::string> const& debugConfigs =
                      this->Makefile->GetCMakeInstance()->GetDebugConfigs();

  std::string configString = "$<CONFIG:" + debugConfigs[0] + ">";

  if (debugConfigs.size() > 1)
    {
    for(std::vector<std::string>::const_iterator
          li = debugConfigs.begin() + 1; li != debugConfigs.end(); ++li)
      {
      configString += ",$<CONFIG:" + *li + ">";
      }
    configString = "$<OR:" + configString + ">";
    }

  if (llt == OPTIMIZED)
    {
    configString = "$<NOT:" + configString + ">";
    }
  return "$<" + configString + ":" + value + ">";
}

//----------------------------------------------------------------------------
static std::string targetNameGenex(const std::string& lib)
{
  return "$<TARGET_NAME:" + lib + ">";
}

//----------------------------------------------------------------------------
bool cmTarget::PushTLLCommandTrace(TLLSignature signature)
{
  bool ret = true;
  if (!this->TLLCommands.empty())
    {
    if (this->TLLCommands.back().first != signature)
      {
      ret = false;
      }
    }
  cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
  this->TLLCommands.push_back(std::make_pair(signature, lfbt));
  return ret;
}

//----------------------------------------------------------------------------
void cmTarget::GetTllSignatureTraces(cmOStringStream &s,
                                     TLLSignature sig) const
{
  std::vector<cmListFileBacktrace> sigs;
  typedef std::vector<std::pair<TLLSignature, cmListFileBacktrace> > Container;
  for(Container::const_iterator it = this->TLLCommands.begin();
      it != this->TLLCommands.end(); ++it)
    {
    if (it->first == sig)
      {
      sigs.push_back(it->second);
      }
    }
  if (!sigs.empty())
    {
    const char *sigString
                        = (sig == cmTarget::KeywordTLLSignature ? "keyword"
                                                                : "plain");
    s << "The uses of the " << sigString << " signature are here:\n";
    std::set<std::string> emitted;
    for(std::vector<cmListFileBacktrace>::iterator it = sigs.begin();
        it != sigs.end(); ++it)
      {
      it->MakeRelative();
      cmListFileBacktrace::const_iterator i = it->begin();
      if(i != it->end())
        {
        cmListFileContext const& lfc = *i;
        cmOStringStream line;
        line << " * " << (lfc.Line? "": " in ") << lfc << std::endl;
        if (emitted.insert(line.str()).second)
          {
          s << line.str();
          }
        ++i;
        }
      }
    }
}

//----------------------------------------------------------------------------
void cmTarget::AddLinkLibrary(cmMakefile& mf,
                              const std::string& target,
                              const std::string& lib,
                              LinkLibraryType llt)
{
  cmTarget *tgt = this->Makefile->FindTargetToUse(lib);
  {
  const bool isNonImportedTarget = tgt && !tgt->IsImported();

  const std::string libName = (isNonImportedTarget && llt != GENERAL)
                                                        ? targetNameGenex(lib)
                                                        : lib;
  this->AppendProperty("LINK_LIBRARIES",
                       this->GetDebugGeneratorExpressions(libName,
                                                          llt).c_str());
  }

  if (cmGeneratorExpression::Find(lib) != std::string::npos
      || (tgt && tgt->GetType() == INTERFACE_LIBRARY)
      || (target == lib ))
    {
    return;
    }

  cmTarget::LibraryID tmp;
  tmp.first = lib;
  tmp.second = llt;
  this->LinkLibraries.push_back( tmp );
  this->OriginalLinkLibraries.push_back(tmp);
  this->ClearLinkMaps();

  // Add the explicit dependency information for this target. This is
  // simply a set of libraries separated by ";". There should always
  // be a trailing ";". These library names are not canonical, in that
  // they may be "-framework x", "-ly", "/path/libz.a", etc.
  // We shouldn't remove duplicates here because external libraries
  // may be purposefully duplicated to handle recursive dependencies,
  // and we removing one instance will break the link line. Duplicates
  // will be appropriately eliminated at emit time.
  if(this->RecordDependencies)
    {
    std::string targetEntry = target;
    targetEntry += "_LIB_DEPENDS";
    std::string dependencies;
    const char* old_val = mf.GetDefinition( targetEntry );
    if( old_val )
      {
      dependencies += old_val;
      }
    switch (llt)
      {
      case cmTarget::GENERAL:
        dependencies += "general";
        break;
      case cmTarget::DEBUG:
        dependencies += "debug";
        break;
      case cmTarget::OPTIMIZED:
        dependencies += "optimized";
        break;
      }
    dependencies += ";";
    dependencies += lib;
    dependencies += ";";
    mf.AddCacheDefinition( targetEntry, dependencies.c_str(),
                           "Dependencies for the target",
                           cmCacheManager::STATIC );
    }

}

//----------------------------------------------------------------------------
void
cmTarget::AddSystemIncludeDirectories(const std::set<std::string> &incs)
{
  for(std::set<std::string>::const_iterator li = incs.begin();
      li != incs.end(); ++li)
    {
    this->SystemIncludeDirectories.insert(*li);
    }
}

//----------------------------------------------------------------------------
void
cmTarget::AddSystemIncludeDirectories(const std::vector<std::string> &incs)
{
  for(std::vector<std::string>::const_iterator li = incs.begin();
      li != incs.end(); ++li)
    {
    this->SystemIncludeDirectories.insert(*li);
    }
}

//----------------------------------------------------------------------------
void
cmTarget::AnalyzeLibDependencies( const cmMakefile& mf )
{
  // There are two key parts of the dependency analysis: (1)
  // determining the libraries in the link line, and (2) constructing
  // the dependency graph for those libraries.
  //
  // The latter is done using the cache entries that record the
  // dependencies of each library.
  //
  // The former is a more thorny issue, since it is not clear how to
  // determine if two libraries listed on the link line refer to the a
  // single library or not. For example, consider the link "libraries"
  //    /usr/lib/libtiff.so -ltiff
  // Is this one library or two? The solution implemented here is the
  // simplest (and probably the only practical) one: two libraries are
  // the same if their "link strings" are identical. Thus, the two
  // libraries above are considered distinct. This also means that for
  // dependency analysis to be effective, the CMake user must specify
  // libraries build by his project without using any linker flags or
  // file extensions. That is,
  //    LINK_LIBRARIES( One Two )
  // instead of
  //    LINK_LIBRARIES( -lOne ${binarypath}/libTwo.a )
  // The former is probably what most users would do, but it never
  // hurts to document the assumptions. :-) Therefore, in the analysis
  // code, the "canonical name" of a library is simply its name as
  // given to a LINK_LIBRARIES command.
  //
  // Also, we will leave the original link line intact; we will just add any
  // dependencies that were missing.
  //
  // There is a problem with recursive external libraries
  // (i.e. libraries with no dependency information that are
  // recursively dependent). We must make sure that the we emit one of
  // the libraries twice to satisfy the recursion, but we shouldn't
  // emit it more times than necessary. In particular, we must make
  // sure that handling this improbable case doesn't cost us when
  // dealing with the common case of non-recursive libraries. The
  // solution is to assume that the recursion is satisfied at one node
  // of the dependency tree. To illustrate, assume libA and libB are
  // extrenal and mutually dependent. Suppose libX depends on
  // libA, and libY on libA and libX. Then
  //   TARGET_LINK_LIBRARIES( Y X A B A )
  //   TARGET_LINK_LIBRARIES( X A B A )
  //   TARGET_LINK_LIBRARIES( Exec Y )
  // would result in "-lY -lX -lA -lB -lA". This is the correct way to
  // specify the dependencies, since the mutual dependency of A and B
  // is resolved *every time libA is specified*.
  //
  // Something like
  //   TARGET_LINK_LIBRARIES( Y X A B A )
  //   TARGET_LINK_LIBRARIES( X A B )
  //   TARGET_LINK_LIBRARIES( Exec Y )
  // would result in "-lY -lX -lA -lB", and the mutual dependency
  // information is lost. This is because in some case (Y), the mutual
  // dependency of A and B is listed, while in another other case (X),
  // it is not. Depending on which line actually emits A, the mutual
  // dependency may or may not be on the final link line.  We can't
  // handle this pathalogical case cleanly without emitting extra
  // libraries for the normal cases. Besides, the dependency
  // information for X is wrong anyway: if we build an executable
  // depending on X alone, we would not have the mutual dependency on
  // A and B resolved.
  //
  // IMPROVEMENTS:
  // -- The current algorithm will not always pick the "optimal" link line
  //    when recursive dependencies are present. It will instead break the
  //    cycles at an aribtrary point. The majority of projects won't have
  //    cyclic dependencies, so this is probably not a big deal. Note that
  //    the link line is always correct, just not necessary optimal.

 {
 // Expand variables in link library names.  This is for backwards
 // compatibility with very early CMake versions and should
 // eventually be removed.  This code was moved here from the end of
 // old source list processing code which was called just before this
 // method.
 for(LinkLibraryVectorType::iterator p = this->LinkLibraries.begin();
     p != this->LinkLibraries.end(); ++p)
   {
   this->Makefile->ExpandVariablesInString(p->first, true, true);
   }
 }

 // The dependency map.
 DependencyMap dep_map;

 // 1. Build the dependency graph
 //
 for(LinkLibraryVectorType::reverse_iterator lib
       = this->LinkLibraries.rbegin();
     lib != this->LinkLibraries.rend(); ++lib)
   {
   this->GatherDependencies( mf, *lib, dep_map);
   }

 // 2. Remove any dependencies that are already satisfied in the original
 // link line.
 //
 for(LinkLibraryVectorType::iterator lib = this->LinkLibraries.begin();
     lib != this->LinkLibraries.end(); ++lib)
   {
   for( LinkLibraryVectorType::iterator lib2 = lib;
        lib2 != this->LinkLibraries.end(); ++lib2)
     {
     this->DeleteDependency( dep_map, *lib, *lib2);
     }
   }


 // 3. Create the new link line by simply emitting any dependencies that are
 // missing.  Start from the back and keep adding.
 //
 std::set<DependencyMap::key_type> done, visited;
 std::vector<DependencyMap::key_type> newLinkLibraries;
 for(LinkLibraryVectorType::reverse_iterator lib =
       this->LinkLibraries.rbegin();
     lib != this->LinkLibraries.rend(); ++lib)
   {
   // skip zero size library entries, this may happen
   // if a variable expands to nothing.
   if (lib->first.size() != 0)
     {
     this->Emit( *lib, dep_map, done, visited, newLinkLibraries );
     }
   }

 // 4. Add the new libraries to the link line.
 //
 for( std::vector<DependencyMap::key_type>::reverse_iterator k =
        newLinkLibraries.rbegin();
      k != newLinkLibraries.rend(); ++k )
   {
   // get the llt from the dep_map
   this->LinkLibraries.push_back( std::make_pair(k->first,k->second) );
   }
 this->LinkLibrariesAnalyzed = true;
}

//----------------------------------------------------------------------------
void cmTarget::InsertDependency( DependencyMap& depMap,
                                 const LibraryID& lib,
                                 const LibraryID& dep)
{
  depMap[lib].push_back(dep);
}

//----------------------------------------------------------------------------
void cmTarget::DeleteDependency( DependencyMap& depMap,
                                 const LibraryID& lib,
                                 const LibraryID& dep)
{
  // Make sure there is an entry in the map for lib. If so, delete all
  // dependencies to dep. There may be repeated entries because of
  // external libraries that are specified multiple times.
  DependencyMap::iterator map_itr = depMap.find( lib );
  if( map_itr != depMap.end() )
    {
    DependencyList& depList = map_itr->second;
    DependencyList::iterator itr;
    while( (itr = std::find(depList.begin(), depList.end(), dep)) !=
           depList.end() )
      {
      depList.erase( itr );
      }
    }
}

//----------------------------------------------------------------------------
void cmTarget::Emit(const LibraryID lib,
                    const DependencyMap& dep_map,
                    std::set<LibraryID>& emitted,
                    std::set<LibraryID>& visited,
                    DependencyList& link_line )
{
  // It's already been emitted
  if( emitted.find(lib) != emitted.end() )
    {
    return;
    }

  // Emit the dependencies only if this library node hasn't been
  // visited before. If it has, then we have a cycle. The recursion
  // that got us here should take care of everything.

  if( visited.insert(lib).second )
    {
    if( dep_map.find(lib) != dep_map.end() ) // does it have dependencies?
      {
      const DependencyList& dep_on = dep_map.find( lib )->second;
      DependencyList::const_reverse_iterator i;

      // To cater for recursive external libraries, we must emit
      // duplicates on this link line *unless* they were emitted by
      // some other node, in which case we assume that the recursion
      // was resolved then. We making the simplifying assumption that
      // any duplicates on a single link line are on purpose, and must
      // be preserved.

      // This variable will keep track of the libraries that were
      // emitted directly from the current node, and not from a
      // recursive call. This way, if we come across a library that
      // has already been emitted, we repeat it iff it has been
      // emitted here.
      std::set<DependencyMap::key_type> emitted_here;
      for( i = dep_on.rbegin(); i != dep_on.rend(); ++i )
        {
        if( emitted_here.find(*i) != emitted_here.end() )
          {
          // a repeat. Must emit.
          emitted.insert(*i);
          link_line.push_back( *i );
          }
        else
          {
          // Emit only if no-one else has
          if( emitted.find(*i) == emitted.end() )
            {
            // emit dependencies
            Emit( *i, dep_map, emitted, visited, link_line );
            // emit self
            emitted.insert(*i);
            emitted_here.insert(*i);
            link_line.push_back( *i );
            }
          }
        }
      }
    }
}

//----------------------------------------------------------------------------
void cmTarget::GatherDependencies( const cmMakefile& mf,
                                   const LibraryID& lib,
                                   DependencyMap& dep_map)
{
  // If the library is already in the dependency map, then it has
  // already been fully processed.
  if( dep_map.find(lib) != dep_map.end() )
    {
    return;
    }

  const char* deps = mf.GetDefinition( lib.first+"_LIB_DEPENDS" );
  if( deps && strcmp(deps,"") != 0 )
    {
    // Make sure this library is in the map, even if it has an empty
    // set of dependencies. This distinguishes the case of explicitly
    // no dependencies with that of unspecified dependencies.
    dep_map[lib];

    // Parse the dependency information, which is a set of
    // type, library pairs separated by ";". There is always a trailing ";".
    cmTarget::LinkLibraryType llt = cmTarget::GENERAL;
    std::string depline = deps;
    std::string::size_type start = 0;
    std::string::size_type end;
    end = depline.find( ";", start );
    while( end != std::string::npos )
      {
      std::string l = depline.substr( start, end-start );
      if( l.size() != 0 )
        {
        if (l == "debug")
          {
          llt = cmTarget::DEBUG;
          }
        else if (l == "optimized")
          {
          llt = cmTarget::OPTIMIZED;
          }
        else if (l == "general")
          {
          llt = cmTarget::GENERAL;
          }
        else
          {
          LibraryID lib2(l,llt);
          this->InsertDependency( dep_map, lib, lib2);
          this->GatherDependencies( mf, lib2, dep_map);
          llt = cmTarget::GENERAL;
          }
        }
      start = end+1; // skip the ;
      end = depline.find( ";", start );
      }
    // cannot depend on itself
    this->DeleteDependency( dep_map, lib, lib);
    }
}

//----------------------------------------------------------------------------
static bool whiteListedInterfaceProperty(const std::string& prop)
{
  if(cmHasLiteralPrefix(prop, "INTERFACE_"))
    {
    return true;
    }
  static const char* builtIns[] = {
    // ###: This must remain sorted. It is processed with a binary search.
    "COMPATIBLE_INTERFACE_BOOL",
    "COMPATIBLE_INTERFACE_NUMBER_MAX",
    "COMPATIBLE_INTERFACE_NUMBER_MIN",
    "COMPATIBLE_INTERFACE_STRING",
    "EXPORT_NAME",
    "IMPORTED",
    "NAME",
    "TYPE"
  };

  if (std::binary_search(cmArrayBegin(builtIns),
                         cmArrayEnd(builtIns),
                         prop.c_str(),
                         cmStrCmp(prop)))
    {
    return true;
    }

  if (cmHasLiteralPrefix(prop, "MAP_IMPORTED_CONFIG_"))
    {
    return true;
    }

  return false;
}

//----------------------------------------------------------------------------
void cmTarget::SetProperty(const std::string& prop, const char* value)
{
  if (this->GetType() == INTERFACE_LIBRARY
      && !whiteListedInterfaceProperty(prop))
    {
    cmOStringStream e;
    e << "INTERFACE_LIBRARY targets may only have whitelisted properties.  "
         "The property \"" << prop << "\" is not allowed.";
    this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
    return;
    }

  if (prop == "NAME")
    {
    cmOStringStream e;
    e << "NAME property is read-only\n";
    this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
    return;
    }
  if(prop == "INCLUDE_DIRECTORIES")
    {
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    deleteAndClear(this->Internal->IncludeDirectoriesEntries);
    cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(value);
    this->Internal->IncludeDirectoriesEntries.push_back(
                          new cmTargetInternals::TargetPropertyEntry(cge));
    return;
    }
  if(prop == "COMPILE_OPTIONS")
    {
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    deleteAndClear(this->Internal->CompileOptionsEntries);
    cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(value);
    this->Internal->CompileOptionsEntries.push_back(
                          new cmTargetInternals::TargetPropertyEntry(cge));
    return;
    }
  if(prop == "COMPILE_FEATURES")
    {
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    deleteAndClear(this->Internal->CompileFeaturesEntries);
    cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(value);
    this->Internal->CompileFeaturesEntries.push_back(
                          new cmTargetInternals::TargetPropertyEntry(cge));
    return;
    }
  if(prop == "COMPILE_DEFINITIONS")
    {
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    deleteAndClear(this->Internal->CompileDefinitionsEntries);
    cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(value);
    this->Internal->CompileDefinitionsEntries.push_back(
                          new cmTargetInternals::TargetPropertyEntry(cge));
    return;
    }
  if(prop == "EXPORT_NAME" && this->IsImported())
    {
    cmOStringStream e;
    e << "EXPORT_NAME property can't be set on imported targets (\""
          << this->Name << "\")\n";
    this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
    return;
    }
  if (prop == "LINK_LIBRARIES")
    {
    this->Internal->LinkImplementationPropertyEntries.clear();
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmValueWithOrigin entry(value, lfbt);
    this->Internal->LinkImplementationPropertyEntries.push_back(entry);
    return;
    }
  if (prop == "SOURCES")
    {
    if(this->IsImported())
      {
      cmOStringStream e;
      e << "SOURCES property can't be set on imported targets (\""
            << this->Name << "\")\n";
      this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
      return;
      }
    this->Internal->SourceFilesMap.clear();
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    this->Internal->SourceEntries.clear();
    cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(value);
    this->Internal->SourceEntries.push_back(
                          new cmTargetInternals::TargetPropertyEntry(cge));
    return;
    }
  this->Properties.SetProperty(prop, value, cmProperty::TARGET);
  this->MaybeInvalidatePropertyCache(prop);
}

//----------------------------------------------------------------------------
void cmTarget::AppendProperty(const std::string& prop, const char* value,
                              bool asString)
{
  if (this->GetType() == INTERFACE_LIBRARY
      && !whiteListedInterfaceProperty(prop))
    {
    cmOStringStream e;
    e << "INTERFACE_LIBRARY targets may only have whitelisted properties.  "
         "The property \"" << prop << "\" is not allowed.";
    this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
    return;
    }
  if (prop == "NAME")
    {
    cmOStringStream e;
    e << "NAME property is read-only\n";
    this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
    return;
    }
  if(prop == "INCLUDE_DIRECTORIES")
    {
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    this->Internal->IncludeDirectoriesEntries.push_back(
              new cmTargetInternals::TargetPropertyEntry(ge.Parse(value)));
    return;
    }
  if(prop == "COMPILE_OPTIONS")
    {
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    this->Internal->CompileOptionsEntries.push_back(
              new cmTargetInternals::TargetPropertyEntry(ge.Parse(value)));
    return;
    }
  if(prop == "COMPILE_FEATURES")
    {
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    this->Internal->CompileFeaturesEntries.push_back(
              new cmTargetInternals::TargetPropertyEntry(ge.Parse(value)));
    return;
    }
  if(prop == "COMPILE_DEFINITIONS")
    {
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmGeneratorExpression ge(&lfbt);
    this->Internal->CompileDefinitionsEntries.push_back(
              new cmTargetInternals::TargetPropertyEntry(ge.Parse(value)));
    return;
    }
  if(prop == "EXPORT_NAME" && this->IsImported())
    {
    cmOStringStream e;
    e << "EXPORT_NAME property can't be set on imported targets (\""
          << this->Name << "\")\n";
    this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
    return;
    }
  if (prop == "LINK_LIBRARIES")
    {
    cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
    cmValueWithOrigin entry(value, lfbt);
    this->Internal->LinkImplementationPropertyEntries.push_back(entry);
    return;
    }
  if (prop == "SOURCES")
    {
    if(this->IsImported())
      {
      cmOStringStream e;
      e << "SOURCES property can't be set on imported targets (\""
            << this->Name << "\")\n";
      this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
      return;
      }
      this->Internal->SourceFilesMap.clear();
      cmListFileBacktrace lfbt = this->Makefile->GetBacktrace();
      cmGeneratorExpression ge(&lfbt);
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(value);
      this->Internal->SourceEntries.push_back(
                            new cmTargetInternals::TargetPropertyEntry(cge));
    return;
    }
  this->Properties.AppendProperty(prop, value, cmProperty::TARGET, asString);
  this->MaybeInvalidatePropertyCache(prop);
}

//----------------------------------------------------------------------------
std::string cmTarget::GetExportName() const
{
  const char *exportName = this->GetProperty("EXPORT_NAME");

  if (exportName && *exportName)
    {
    if (!cmGeneratorExpression::IsValidTargetName(exportName))
      {
      cmOStringStream e;
      e << "EXPORT_NAME property \"" << exportName << "\" for \""
        << this->GetName() << "\": is not valid.";
      cmSystemTools::Error(e.str().c_str());
      return "";
      }
    return exportName;
    }
  return this->GetName();
}

//----------------------------------------------------------------------------
void cmTarget::AppendBuildInterfaceIncludes()
{
  if(this->GetType() != cmTarget::SHARED_LIBRARY &&
     this->GetType() != cmTarget::STATIC_LIBRARY &&
     this->GetType() != cmTarget::MODULE_LIBRARY &&
     this->GetType() != cmTarget::INTERFACE_LIBRARY &&
     !this->IsExecutableWithExports())
    {
    return;
    }
  if (this->BuildInterfaceIncludesAppended)
    {
    return;
    }
  this->BuildInterfaceIncludesAppended = true;

  if (this->Makefile->IsOn("CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE"))
    {
    const char *binDir = this->Makefile->GetStartOutputDirectory();
    const char *srcDir = this->Makefile->GetStartDirectory();
    const std::string dirs = std::string(binDir ? binDir : "")
                            + std::string(binDir ? ";" : "")
                            + std::string(srcDir ? srcDir : "");
    if (!dirs.empty())
      {
      this->AppendProperty("INTERFACE_INCLUDE_DIRECTORIES",
                            ("$<BUILD_INTERFACE:" + dirs + ">").c_str());
      }
    }
}

//----------------------------------------------------------------------------
void cmTarget::InsertInclude(const cmValueWithOrigin &entry,
                     bool before)
{
  cmGeneratorExpression ge(&entry.Backtrace);

  std::vector<cmTargetInternals::TargetPropertyEntry*>::iterator position
                = before ? this->Internal->IncludeDirectoriesEntries.begin()
                         : this->Internal->IncludeDirectoriesEntries.end();

  this->Internal->IncludeDirectoriesEntries.insert(position,
      new cmTargetInternals::TargetPropertyEntry(ge.Parse(entry.Value)));
}

//----------------------------------------------------------------------------
void cmTarget::InsertCompileOption(const cmValueWithOrigin &entry,
                     bool before)
{
  cmGeneratorExpression ge(&entry.Backtrace);

  std::vector<cmTargetInternals::TargetPropertyEntry*>::iterator position
                = before ? this->Internal->CompileOptionsEntries.begin()
                         : this->Internal->CompileOptionsEntries.end();

  this->Internal->CompileOptionsEntries.insert(position,
      new cmTargetInternals::TargetPropertyEntry(ge.Parse(entry.Value)));
}

//----------------------------------------------------------------------------
void cmTarget::InsertCompileDefinition(const cmValueWithOrigin &entry)
{
  cmGeneratorExpression ge(&entry.Backtrace);

  this->Internal->CompileDefinitionsEntries.push_back(
      new cmTargetInternals::TargetPropertyEntry(ge.Parse(entry.Value)));
}

//----------------------------------------------------------------------------
static void processIncludeDirectories(cmTarget const* tgt,
      const std::vector<cmTargetInternals::TargetPropertyEntry*> &entries,
      std::vector<std::string> &includes,
      std::set<std::string> &uniqueIncludes,
      cmGeneratorExpressionDAGChecker *dagChecker,
      const std::string& config, bool debugIncludes)
{
  cmMakefile *mf = tgt->GetMakefile();

  for (std::vector<cmTargetInternals::TargetPropertyEntry*>::const_iterator
      it = entries.begin(), end = entries.end(); it != end; ++it)
    {
    bool testIsOff = true;
    bool cacheIncludes = false;
    std::vector<std::string>& entryIncludes = (*it)->CachedEntries;
    if(!entryIncludes.empty())
      {
      testIsOff = false;
      }
    else
      {
      cmSystemTools::ExpandListArgument((*it)->ge->Evaluate(mf,
                                                config,
                                                false,
                                                tgt,
                                                dagChecker),
                                      entryIncludes);
      if (mf->IsGeneratingBuildSystem()
          && !(*it)->ge->GetHadContextSensitiveCondition())
        {
        cacheIncludes = true;
        }
      }
    std::string usedIncludes;
    for(std::vector<std::string>::iterator
          li = entryIncludes.begin(); li != entryIncludes.end(); ++li)
      {
      std::string targetName = (*it)->TargetName;
      std::string evaluatedTargetName;
      {
      cmGeneratorExpression ge;
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge =
                                                        ge.Parse(targetName);
      evaluatedTargetName = cge->Evaluate(mf, config, false, tgt, 0, 0);
      }

      cmTarget *dependentTarget = mf->FindTargetToUse(targetName);

      const bool fromImported = dependentTarget
                             && dependentTarget->IsImported();

      cmTarget *evaluatedDependentTarget =
        (targetName != evaluatedTargetName)
          ? mf->FindTargetToUse(evaluatedTargetName)
          : 0;

      targetName = evaluatedTargetName;

      const bool fromEvaluatedImported = evaluatedDependentTarget
                             && evaluatedDependentTarget->IsImported();

      if ((fromImported || fromEvaluatedImported)
          && !cmSystemTools::FileExists(li->c_str()))
        {
        cmOStringStream e;
        cmake::MessageType messageType = cmake::FATAL_ERROR;
        if (fromEvaluatedImported)
          {
          switch(tgt->GetPolicyStatusCMP0027())
            {
            case cmPolicies::WARN:
              e << (mf->GetPolicies()
                    ->GetPolicyWarning(cmPolicies::CMP0027)) << "\n";
            case cmPolicies::OLD:
              messageType = cmake::AUTHOR_WARNING;
              break;
            case cmPolicies::REQUIRED_ALWAYS:
            case cmPolicies::REQUIRED_IF_USED:
            case cmPolicies::NEW:
              break;
            }
          }
        e << "Imported target \"" << targetName << "\" includes "
             "non-existent path\n  \"" << *li << "\"\nin its "
             "INTERFACE_INCLUDE_DIRECTORIES. Possible reasons include:\n"
             "* The path was deleted, renamed, or moved to another "
             "location.\n"
             "* An install or uninstall procedure did not complete "
             "successfully.\n"
             "* The installation package was faulty and references files it "
             "does not provide.\n";
        tgt->GetMakefile()->IssueMessage(messageType, e.str());
        return;
        }

      if (!cmSystemTools::FileIsFullPath(li->c_str()))
        {
        cmOStringStream e;
        bool noMessage = false;
        cmake::MessageType messageType = cmake::FATAL_ERROR;
        if (!targetName.empty())
          {
          e << "Target \"" << targetName << "\" contains relative "
            "path in its INTERFACE_INCLUDE_DIRECTORIES:\n"
            "  \"" << *li << "\"";
          }
        else
          {
          switch(tgt->GetPolicyStatusCMP0021())
            {
            case cmPolicies::WARN:
              {
              e << (mf->GetPolicies()
                    ->GetPolicyWarning(cmPolicies::CMP0021)) << "\n";
              messageType = cmake::AUTHOR_WARNING;
              }
              break;
            case cmPolicies::OLD:
              noMessage = true;
            case cmPolicies::REQUIRED_IF_USED:
            case cmPolicies::REQUIRED_ALWAYS:
            case cmPolicies::NEW:
              // Issue the fatal message.
              break;
            }
          e << "Found relative path while evaluating include directories of "
          "\"" << tgt->GetName() << "\":\n  \"" << *li << "\"\n";
          }
        if (!noMessage)
          {
          tgt->GetMakefile()->IssueMessage(messageType, e.str());
          if (messageType == cmake::FATAL_ERROR)
            {
            return;
            }
          }
        }

      if (testIsOff && !cmSystemTools::IsOff(li->c_str()))
        {
        cmSystemTools::ConvertToUnixSlashes(*li);
        }
      std::string inc = *li;

      if(uniqueIncludes.insert(inc).second)
        {
        includes.push_back(inc);
        if (debugIncludes)
          {
          usedIncludes += " * " + inc + "\n";
          }
        }
      }
    if (cacheIncludes)
      {
      (*it)->CachedEntries = entryIncludes;
      }
    if (!usedIncludes.empty())
      {
      mf->GetCMakeInstance()->IssueMessage(cmake::LOG,
                            std::string("Used includes for target ")
                            + tgt->GetName() + ":\n"
                            + usedIncludes, (*it)->ge->GetBacktrace());
      }
    }
}

//----------------------------------------------------------------------------
std::vector<std::string>
cmTarget::GetIncludeDirectories(const std::string& config) const
{
  std::vector<std::string> includes;
  std::set<std::string> uniqueIncludes;

  cmGeneratorExpressionDAGChecker dagChecker(this->GetName(),
                                             "INCLUDE_DIRECTORIES", 0, 0);

  std::vector<std::string> debugProperties;
  const char *debugProp =
              this->Makefile->GetDefinition("CMAKE_DEBUG_TARGET_PROPERTIES");
  if (debugProp)
    {
    cmSystemTools::ExpandListArgument(debugProp, debugProperties);
    }

  bool debugIncludes = !this->DebugIncludesDone
                    && std::find(debugProperties.begin(),
                                 debugProperties.end(),
                                 "INCLUDE_DIRECTORIES")
                        != debugProperties.end();

  if (this->Makefile->IsGeneratingBuildSystem())
    {
    this->DebugIncludesDone = true;
    }

  processIncludeDirectories(this,
                            this->Internal->IncludeDirectoriesEntries,
                            includes,
                            uniqueIncludes,
                            &dagChecker,
                            config,
                            debugIncludes);

  if (!this->Internal->CacheLinkInterfaceIncludeDirectoriesDone[config])
    {
    for (std::vector<cmValueWithOrigin>::const_iterator
        it = this->Internal->LinkImplementationPropertyEntries.begin(),
        end = this->Internal->LinkImplementationPropertyEntries.end();
        it != end; ++it)
      {
      if (!cmGeneratorExpression::IsValidTargetName(it->Value)
          && cmGeneratorExpression::Find(it->Value) == std::string::npos)
        {
        continue;
        }
      {
      cmGeneratorExpression ge;
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge =
                                                        ge.Parse(it->Value);
      std::string result = cge->Evaluate(this->Makefile, config,
                                        false, this, 0, 0);
      if (!this->Makefile->FindTargetToUse(result))
        {
        continue;
        }
      }
      std::string includeGenex = "$<TARGET_PROPERTY:" +
                              it->Value + ",INTERFACE_INCLUDE_DIRECTORIES>";
      if (cmGeneratorExpression::Find(it->Value) != std::string::npos)
        {
        // Because it->Value is a generator expression, ensure that it
        // evaluates to the non-empty string before being used in the
        // TARGET_PROPERTY expression.
        includeGenex = "$<$<BOOL:" + it->Value + ">:" + includeGenex + ">";
        }
      cmGeneratorExpression ge(&it->Backtrace);
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(
                                                              includeGenex);

      this->Internal
        ->CachedLinkInterfaceIncludeDirectoriesEntries[config].push_back(
                        new cmTargetInternals::TargetPropertyEntry(cge,
                                                              it->Value));
      }

    if(this->Makefile->IsOn("APPLE"))
      {
      LinkImplementation const* impl = this->GetLinkImplementation(config);
      for(std::vector<cmLinkItem>::const_iterator
          it = impl->Libraries.begin();
          it != impl->Libraries.end(); ++it)
        {
        std::string libDir = cmSystemTools::CollapseFullPath(it->c_str());

        static cmsys::RegularExpression
          frameworkCheck("(.*\\.framework)(/Versions/[^/]+)?/[^/]+$");
        if(!frameworkCheck.find(libDir))
          {
          continue;
          }

        libDir = frameworkCheck.match(1);

        cmGeneratorExpression ge;
        cmsys::auto_ptr<cmCompiledGeneratorExpression> cge =
                  ge.Parse(libDir.c_str());
        this->Internal
                ->CachedLinkInterfaceIncludeDirectoriesEntries[config]
                .push_back(new cmTargetInternals::TargetPropertyEntry(cge));
        }
      }
    }

  processIncludeDirectories(this,
    this->Internal->CachedLinkInterfaceIncludeDirectoriesEntries[config],
                            includes,
                            uniqueIncludes,
                            &dagChecker,
                            config,
                            debugIncludes);

  if (!this->Makefile->IsGeneratingBuildSystem())
    {
    deleteAndClear(
                this->Internal->CachedLinkInterfaceIncludeDirectoriesEntries);
    }
  else
    {
    this->Internal->CacheLinkInterfaceIncludeDirectoriesDone[config]
                                                                      = true;
    }

  return includes;
}

//----------------------------------------------------------------------------
static void processCompileOptionsInternal(cmTarget const* tgt,
      const std::vector<cmTargetInternals::TargetPropertyEntry*> &entries,
      std::vector<std::string> &options,
      std::set<std::string> &uniqueOptions,
      cmGeneratorExpressionDAGChecker *dagChecker,
      const std::string& config, bool debugOptions, const char *logName)
{
  cmMakefile *mf = tgt->GetMakefile();

  for (std::vector<cmTargetInternals::TargetPropertyEntry*>::const_iterator
      it = entries.begin(), end = entries.end(); it != end; ++it)
    {
    bool cacheOptions = false;
    std::vector<std::string> entryOptions = (*it)->CachedEntries;
    if(entryOptions.empty())
      {
      cmSystemTools::ExpandListArgument((*it)->ge->Evaluate(mf,
                                                config,
                                                false,
                                                tgt,
                                                dagChecker),
                                      entryOptions);
      if (mf->IsGeneratingBuildSystem()
          && !(*it)->ge->GetHadContextSensitiveCondition())
        {
        cacheOptions = true;
        }
      }
    std::string usedOptions;
    for(std::vector<std::string>::iterator
          li = entryOptions.begin(); li != entryOptions.end(); ++li)
      {
      std::string opt = *li;

      if(uniqueOptions.insert(opt).second)
        {
        options.push_back(opt);
        if (debugOptions)
          {
          usedOptions += " * " + opt + "\n";
          }
        }
      }
    if (cacheOptions)
      {
      (*it)->CachedEntries = entryOptions;
      }
    if (!usedOptions.empty())
      {
      mf->GetCMakeInstance()->IssueMessage(cmake::LOG,
                            std::string("Used compile ") + logName
                            + std::string(" for target ")
                            + tgt->GetName() + ":\n"
                            + usedOptions, (*it)->ge->GetBacktrace());
      }
    }
}

//----------------------------------------------------------------------------
static void processCompileOptions(cmTarget const* tgt,
      const std::vector<cmTargetInternals::TargetPropertyEntry*> &entries,
      std::vector<std::string> &options,
      std::set<std::string> &uniqueOptions,
      cmGeneratorExpressionDAGChecker *dagChecker,
      const std::string& config, bool debugOptions)
{
  processCompileOptionsInternal(tgt, entries, options, uniqueOptions,
                                dagChecker, config, debugOptions, "options");
}

//----------------------------------------------------------------------------
void cmTarget::GetAutoUicOptions(std::vector<std::string> &result,
                                 const std::string& config) const
{
  const char *prop
            = this->GetLinkInterfaceDependentStringProperty("AUTOUIC_OPTIONS",
                                                            config);
  if (!prop)
    {
    return;
    }
  cmGeneratorExpression ge;

  cmGeneratorExpressionDAGChecker dagChecker(
                                      this->GetName(),
                                      "AUTOUIC_OPTIONS", 0, 0);
  cmSystemTools::ExpandListArgument(ge.Parse(prop)
                                      ->Evaluate(this->Makefile,
                                                config,
                                                false,
                                                this,
                                                &dagChecker),
                                  result);
}

//----------------------------------------------------------------------------
void cmTarget::GetCompileOptions(std::vector<std::string> &result,
                                 const std::string& config) const
{
  std::set<std::string> uniqueOptions;

  cmGeneratorExpressionDAGChecker dagChecker(this->GetName(),
                                             "COMPILE_OPTIONS", 0, 0);

  std::vector<std::string> debugProperties;
  const char *debugProp =
              this->Makefile->GetDefinition("CMAKE_DEBUG_TARGET_PROPERTIES");
  if (debugProp)
    {
    cmSystemTools::ExpandListArgument(debugProp, debugProperties);
    }

  bool debugOptions = !this->DebugCompileOptionsDone
                    && std::find(debugProperties.begin(),
                                 debugProperties.end(),
                                 "COMPILE_OPTIONS")
                        != debugProperties.end();

  if (this->Makefile->IsGeneratingBuildSystem())
    {
    this->DebugCompileOptionsDone = true;
    }

  processCompileOptions(this,
                            this->Internal->CompileOptionsEntries,
                            result,
                            uniqueOptions,
                            &dagChecker,
                            config,
                            debugOptions);

  if (!this->Internal->CacheLinkInterfaceCompileOptionsDone[config])
    {
    for (std::vector<cmValueWithOrigin>::const_iterator
        it = this->Internal->LinkImplementationPropertyEntries.begin(),
        end = this->Internal->LinkImplementationPropertyEntries.end();
        it != end; ++it)
      {
      if (!cmGeneratorExpression::IsValidTargetName(it->Value)
          && cmGeneratorExpression::Find(it->Value) == std::string::npos)
        {
        continue;
        }
      {
      cmGeneratorExpression ge;
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge =
                                                        ge.Parse(it->Value);
      std::string targetResult = cge->Evaluate(this->Makefile, config,
                                        false, this, 0, 0);
      if (!this->Makefile->FindTargetToUse(targetResult))
        {
        continue;
        }
      }
      std::string optionGenex = "$<TARGET_PROPERTY:" +
                              it->Value + ",INTERFACE_COMPILE_OPTIONS>";
      if (cmGeneratorExpression::Find(it->Value) != std::string::npos)
        {
        // Because it->Value is a generator expression, ensure that it
        // evaluates to the non-empty string before being used in the
        // TARGET_PROPERTY expression.
        optionGenex = "$<$<BOOL:" + it->Value + ">:" + optionGenex + ">";
        }
      cmGeneratorExpression ge(&it->Backtrace);
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(
                                                                optionGenex);

      this->Internal
        ->CachedLinkInterfaceCompileOptionsEntries[config].push_back(
                        new cmTargetInternals::TargetPropertyEntry(cge,
                                                              it->Value));
      }
    }

  processCompileOptions(this,
    this->Internal->CachedLinkInterfaceCompileOptionsEntries[config],
                            result,
                            uniqueOptions,
                            &dagChecker,
                            config,
                            debugOptions);

  if (!this->Makefile->IsGeneratingBuildSystem())
    {
    deleteAndClear(this->Internal->CachedLinkInterfaceCompileOptionsEntries);
    }
  else
    {
    this->Internal->CacheLinkInterfaceCompileOptionsDone[config] = true;
    }
}

//----------------------------------------------------------------------------
static void processCompileDefinitions(cmTarget const* tgt,
      const std::vector<cmTargetInternals::TargetPropertyEntry*> &entries,
      std::vector<std::string> &options,
      std::set<std::string> &uniqueOptions,
      cmGeneratorExpressionDAGChecker *dagChecker,
      const std::string& config, bool debugOptions)
{
  processCompileOptionsInternal(tgt, entries, options, uniqueOptions,
                                dagChecker, config, debugOptions,
                                "definitions");
}

//----------------------------------------------------------------------------
void cmTarget::GetCompileDefinitions(std::vector<std::string> &list,
                                            const std::string& config) const
{
  std::set<std::string> uniqueOptions;

  cmGeneratorExpressionDAGChecker dagChecker(this->GetName(),
                                             "COMPILE_DEFINITIONS", 0, 0);

  std::vector<std::string> debugProperties;
  const char *debugProp =
              this->Makefile->GetDefinition("CMAKE_DEBUG_TARGET_PROPERTIES");
  if (debugProp)
    {
    cmSystemTools::ExpandListArgument(debugProp, debugProperties);
    }

  bool debugDefines = !this->DebugCompileDefinitionsDone
                          && std::find(debugProperties.begin(),
                                debugProperties.end(),
                                "COMPILE_DEFINITIONS")
                        != debugProperties.end();

  if (this->Makefile->IsGeneratingBuildSystem())
    {
    this->DebugCompileDefinitionsDone = true;
    }

  processCompileDefinitions(this,
                            this->Internal->CompileDefinitionsEntries,
                            list,
                            uniqueOptions,
                            &dagChecker,
                            config,
                            debugDefines);

  if (!this->Internal->CacheLinkInterfaceCompileDefinitionsDone[config])
    {
    for (std::vector<cmValueWithOrigin>::const_iterator
        it = this->Internal->LinkImplementationPropertyEntries.begin(),
        end = this->Internal->LinkImplementationPropertyEntries.end();
        it != end; ++it)
      {
      if (!cmGeneratorExpression::IsValidTargetName(it->Value)
          && cmGeneratorExpression::Find(it->Value) == std::string::npos)
        {
        continue;
        }
      {
      cmGeneratorExpression ge;
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge =
                                                        ge.Parse(it->Value);
      std::string targetResult = cge->Evaluate(this->Makefile, config,
                                        false, this, 0, 0);
      if (!this->Makefile->FindTargetToUse(targetResult))
        {
        continue;
        }
      }
      std::string defsGenex = "$<TARGET_PROPERTY:" +
                              it->Value + ",INTERFACE_COMPILE_DEFINITIONS>";
      if (cmGeneratorExpression::Find(it->Value) != std::string::npos)
        {
        // Because it->Value is a generator expression, ensure that it
        // evaluates to the non-empty string before being used in the
        // TARGET_PROPERTY expression.
        defsGenex = "$<$<BOOL:" + it->Value + ">:" + defsGenex + ">";
        }
      cmGeneratorExpression ge(&it->Backtrace);
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(
                                                                defsGenex);

      this->Internal
        ->CachedLinkInterfaceCompileDefinitionsEntries[config].push_back(
                        new cmTargetInternals::TargetPropertyEntry(cge,
                                                              it->Value));
      }
    if (!config.empty())
      {
      std::string configPropName = "COMPILE_DEFINITIONS_"
                                          + cmSystemTools::UpperCase(config);
      const char *configProp = this->GetProperty(configPropName);
      if (configProp)
        {
        switch(this->Makefile->GetPolicyStatus(cmPolicies::CMP0043))
          {
          case cmPolicies::WARN:
            {
            cmOStringStream e;
            e << this->Makefile->GetCMakeInstance()->GetPolicies()
                     ->GetPolicyWarning(cmPolicies::CMP0043);
            this->Makefile->IssueMessage(cmake::AUTHOR_WARNING,
                                         e.str());
            }
          case cmPolicies::OLD:
            {
            cmGeneratorExpression ge;
            cmsys::auto_ptr<cmCompiledGeneratorExpression> cge =
                                                        ge.Parse(configProp);
            this->Internal
              ->CachedLinkInterfaceCompileDefinitionsEntries[config]
                  .push_back(new cmTargetInternals::TargetPropertyEntry(cge));
            }
            break;
          case cmPolicies::NEW:
          case cmPolicies::REQUIRED_ALWAYS:
          case cmPolicies::REQUIRED_IF_USED:
            break;
          }
        }
      }

    }

  processCompileDefinitions(this,
    this->Internal->CachedLinkInterfaceCompileDefinitionsEntries[config],
                            list,
                            uniqueOptions,
                            &dagChecker,
                            config,
                            debugDefines);

  if (!this->Makefile->IsGeneratingBuildSystem())
    {
    deleteAndClear(this->Internal
                              ->CachedLinkInterfaceCompileDefinitionsEntries);
    }
  else
    {
    this->Internal->CacheLinkInterfaceCompileDefinitionsDone[config]
                                                                      = true;
    }
}

//----------------------------------------------------------------------------
static void processCompileFeatures(cmTarget const* tgt,
      const std::vector<cmTargetInternals::TargetPropertyEntry*> &entries,
      std::vector<std::string> &options,
      std::set<std::string> &uniqueOptions,
      cmGeneratorExpressionDAGChecker *dagChecker,
      const std::string& config, bool debugOptions)
{
  processCompileOptionsInternal(tgt, entries, options, uniqueOptions,
                                dagChecker, config, debugOptions, "features");
}

//----------------------------------------------------------------------------
void cmTarget::GetCompileFeatures(std::vector<std::string> &result,
                                  const std::string& config) const
{
  std::set<std::string> uniqueFeatures;

  cmGeneratorExpressionDAGChecker dagChecker(this->GetName(),
                                             "COMPILE_FEATURES",
                                             0, 0);

  std::vector<std::string> debugProperties;
  const char *debugProp =
              this->Makefile->GetDefinition("CMAKE_DEBUG_TARGET_PROPERTIES");
  if (debugProp)
    {
    cmSystemTools::ExpandListArgument(debugProp, debugProperties);
    }

  bool debugFeatures = !this->DebugCompileFeaturesDone
                    && std::find(debugProperties.begin(),
                                 debugProperties.end(),
                                 "COMPILE_FEATURES")
                        != debugProperties.end();

  if (this->Makefile->IsGeneratingBuildSystem())
    {
    this->DebugCompileFeaturesDone = true;
    }

  processCompileFeatures(this,
                            this->Internal->CompileFeaturesEntries,
                            result,
                            uniqueFeatures,
                            &dagChecker,
                            config,
                            debugFeatures);

  if (!this->Internal->CacheLinkInterfaceCompileFeaturesDone[config])
    {
    for (std::vector<cmValueWithOrigin>::const_iterator
        it = this->Internal->LinkImplementationPropertyEntries.begin(),
        end = this->Internal->LinkImplementationPropertyEntries.end();
        it != end; ++it)
      {
      if (!cmGeneratorExpression::IsValidTargetName(it->Value)
          && cmGeneratorExpression::Find(it->Value) == std::string::npos)
        {
        continue;
        }
      {
      cmGeneratorExpression ge;
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge =
                                                        ge.Parse(it->Value);
      std::string targetResult = cge->Evaluate(this->Makefile, config,
                                        false, this, 0, 0);
      if (!this->Makefile->FindTargetToUse(targetResult))
        {
        continue;
        }
      }
      std::string featureGenex = "$<TARGET_PROPERTY:" +
                              it->Value + ",INTERFACE_COMPILE_FEATURES>";
      if (cmGeneratorExpression::Find(it->Value) != std::string::npos)
        {
        // Because it->Value is a generator expression, ensure that it
        // evaluates to the non-empty string before being used in the
        // TARGET_PROPERTY expression.
        featureGenex = "$<$<BOOL:" + it->Value + ">:" + featureGenex + ">";
        }
      cmGeneratorExpression ge(&it->Backtrace);
      cmsys::auto_ptr<cmCompiledGeneratorExpression> cge = ge.Parse(
                                                                featureGenex);

      this->Internal
        ->CachedLinkInterfaceCompileFeaturesEntries[config].push_back(
                        new cmTargetInternals::TargetPropertyEntry(cge,
                                                              it->Value));
      }
    }

  processCompileFeatures(this,
    this->Internal->CachedLinkInterfaceCompileFeaturesEntries[config],
                            result,
                            uniqueFeatures,
                            &dagChecker,
                            config,
                            debugFeatures);

  if (!this->Makefile->IsGeneratingBuildSystem())
    {
    deleteAndClear(this->Internal->CachedLinkInterfaceCompileFeaturesEntries);
    }
  else
    {
    this->Internal->CacheLinkInterfaceCompileFeaturesDone[config] = true;
    }
}

//----------------------------------------------------------------------------
void cmTarget::MaybeInvalidatePropertyCache(const std::string& prop)
{
  // Wipe out maps caching information affected by this property.
  if(this->IsImported() && cmHasLiteralPrefix(prop, "IMPORTED"))
    {
    this->Internal->ImportInfoMap.clear();
    }
  if(!this->IsImported() && cmHasLiteralPrefix(prop, "LINK_INTERFACE_"))
    {
    this->ClearLinkMaps();
    }
}

//----------------------------------------------------------------------------
static void cmTargetCheckLINK_INTERFACE_LIBRARIES(
  const std::string& prop, const char* value, cmMakefile* context,
  bool imported)
{
  // Look for link-type keywords in the value.
  static cmsys::RegularExpression
    keys("(^|;)(debug|optimized|general)(;|$)");
  if(!keys.find(value))
    {
    return;
    }

  // Support imported and non-imported versions of the property.
  const char* base = (imported?
                      "IMPORTED_LINK_INTERFACE_LIBRARIES" :
                      "LINK_INTERFACE_LIBRARIES");

  // Report an error.
  cmOStringStream e;
  e << "Property " << prop << " may not contain link-type keyword \""
    << keys.match(2) << "\".  "
    << "The " << base << " property has a per-configuration "
    << "version called " << base << "_<CONFIG> which may be "
    << "used to specify per-configuration rules.";
  if(!imported)
    {
    e << "  "
      << "Alternatively, an IMPORTED library may be created, configured "
      << "with a per-configuration location, and then named in the "
      << "property value.  "
      << "See the add_library command's IMPORTED mode for details."
      << "\n"
      << "If you have a list of libraries that already contains the "
      << "keyword, use the target_link_libraries command with its "
      << "LINK_INTERFACE_LIBRARIES mode to set the property.  "
      << "The command automatically recognizes link-type keywords and sets "
      << "the LINK_INTERFACE_LIBRARIES and LINK_INTERFACE_LIBRARIES_DEBUG "
      << "properties accordingly.";
    }
  context->IssueMessage(cmake::FATAL_ERROR, e.str());
}

//----------------------------------------------------------------------------
static void cmTargetCheckINTERFACE_LINK_LIBRARIES(const char* value,
                                                  cmMakefile* context)
{
  // Look for link-type keywords in the value.
  static cmsys::RegularExpression
    keys("(^|;)(debug|optimized|general)(;|$)");
  if(!keys.find(value))
    {
    return;
    }

  // Report an error.
  cmOStringStream e;

  e << "Property INTERFACE_LINK_LIBRARIES may not contain link-type "
    "keyword \"" << keys.match(2) << "\".  The INTERFACE_LINK_LIBRARIES "
    "property may contain configuration-sensitive generator-expressions "
    "which may be used to specify per-configuration rules.";

  context->IssueMessage(cmake::FATAL_ERROR, e.str());
}

//----------------------------------------------------------------------------
void cmTarget::CheckProperty(const std::string& prop,
                             cmMakefile* context) const
{
  // Certain properties need checking.
  if(cmHasLiteralPrefix(prop, "LINK_INTERFACE_LIBRARIES"))
    {
    if(const char* value = this->GetProperty(prop))
      {
      cmTargetCheckLINK_INTERFACE_LIBRARIES(prop, value, context, false);
      }
    }
  if(cmHasLiteralPrefix(prop, "IMPORTED_LINK_INTERFACE_LIBRARIES"))
    {
    if(const char* value = this->GetProperty(prop))
      {
      cmTargetCheckLINK_INTERFACE_LIBRARIES(prop, value, context, true);
      }
    }
  if(cmHasLiteralPrefix(prop, "INTERFACE_LINK_LIBRARIES"))
    {
    if(const char* value = this->GetProperty(prop))
      {
      cmTargetCheckINTERFACE_LINK_LIBRARIES(value, context);
      }
    }
}

//----------------------------------------------------------------------------
void cmTarget::MarkAsImported()
{
  this->IsImportedTarget = true;
}

//----------------------------------------------------------------------------
bool cmTarget::HaveWellDefinedOutputFiles() const
{
  return
    this->GetType() == cmTarget::STATIC_LIBRARY ||
    this->GetType() == cmTarget::SHARED_LIBRARY ||
    this->GetType() == cmTarget::MODULE_LIBRARY ||
    this->GetType() == cmTarget::EXECUTABLE;
}

//----------------------------------------------------------------------------
cmTarget::OutputInfo const* cmTarget::GetOutputInfo(
    const std::string& config) const
{
  // There is no output information for imported targets.
  if(this->IsImported())
    {
    return 0;
    }

  // Only libraries and executables have well-defined output files.
  if(!this->HaveWellDefinedOutputFiles())
    {
    std::string msg = "cmTarget::GetOutputInfo called for ";
    msg += this->GetName();
    msg += " which has type ";
    msg += cmTarget::GetTargetTypeName(this->GetType());
    this->GetMakefile()->IssueMessage(cmake::INTERNAL_ERROR, msg);
    return 0;
    }

  // Lookup/compute/cache the output information for this configuration.
  std::string config_upper;
  if(!config.empty())
    {
    config_upper = cmSystemTools::UpperCase(config);
    }
  typedef cmTargetInternals::OutputInfoMapType OutputInfoMapType;
  OutputInfoMapType::const_iterator i =
    this->Internal->OutputInfoMap.find(config_upper);
  if(i == this->Internal->OutputInfoMap.end())
    {
    OutputInfo info;
    this->ComputeOutputDir(config, false, info.OutDir);
    this->ComputeOutputDir(config, true, info.ImpDir);
    if(!this->ComputePDBOutputDir("PDB", config, info.PdbDir))
      {
      info.PdbDir = info.OutDir;
      }
    OutputInfoMapType::value_type entry(config_upper, info);
    i = this->Internal->OutputInfoMap.insert(entry).first;
    }
  return &i->second;
}

//----------------------------------------------------------------------------
cmTarget::CompileInfo const* cmTarget::GetCompileInfo(
                                            const std::string& config) const
{
  // There is no compile information for imported targets.
  if(this->IsImported())
    {
    return 0;
    }

  if(this->GetType() > cmTarget::OBJECT_LIBRARY)
    {
    std::string msg = "cmTarget::GetCompileInfo called for ";
    msg += this->GetName();
    msg += " which has type ";
    msg += cmTarget::GetTargetTypeName(this->GetType());
    this->GetMakefile()->IssueMessage(cmake::INTERNAL_ERROR, msg);
    return 0;
    }

  // Lookup/compute/cache the compile information for this configuration.
  std::string config_upper;
  if(!config.empty())
    {
    config_upper = cmSystemTools::UpperCase(config);
    }
  typedef cmTargetInternals::CompileInfoMapType CompileInfoMapType;
  CompileInfoMapType::const_iterator i =
    this->Internal->CompileInfoMap.find(config_upper);
  if(i == this->Internal->CompileInfoMap.end())
    {
    CompileInfo info;
    this->ComputePDBOutputDir("COMPILE_PDB", config, info.CompilePdbDir);
    CompileInfoMapType::value_type entry(config_upper, info);
    i = this->Internal->CompileInfoMap.insert(entry).first;
    }
  return &i->second;
}

//----------------------------------------------------------------------------
std::string cmTarget::GetDirectory(const std::string& config,
                                   bool implib) const
{
  if (this->IsImported())
    {
    // Return the directory from which the target is imported.
    return
      cmSystemTools::GetFilenamePath(
      this->ImportedGetFullPath(config, implib));
    }
  else if(OutputInfo const* info = this->GetOutputInfo(config))
    {
    // Return the directory in which the target will be built.
    return implib? info->ImpDir : info->OutDir;
    }
  return "";
}

//----------------------------------------------------------------------------
std::string cmTarget::GetPDBDirectory(const std::string& config) const
{
  if(OutputInfo const* info = this->GetOutputInfo(config))
    {
    // Return the directory in which the target will be built.
    return info->PdbDir;
    }
  return "";
}

//----------------------------------------------------------------------------
std::string cmTarget::GetCompilePDBDirectory(const std::string& config) const
{
  if(CompileInfo const* info = this->GetCompileInfo(config))
    {
    return info->CompilePdbDir;
    }
  return "";
}

//----------------------------------------------------------------------------
const char* cmTarget::GetLocation(const std::string& config) const
{
  static std::string location;
  if (this->IsImported())
    {
    location = this->ImportedGetFullPath(config, false);
    }
  else
    {
    location = this->GetFullPath(config, false);
    }
  return location.c_str();
}

//----------------------------------------------------------------------------
const char* cmTarget::GetLocationForBuild() const
{
  static std::string location;
  if(this->IsImported())
    {
    location = this->ImportedGetFullPath("", false);
    return location.c_str();
    }

  // Now handle the deprecated build-time configuration location.
  location = this->GetDirectory();
  if(!location.empty())
    {
    location += "/";
    }
  const char* cfgid = this->Makefile->GetDefinition("CMAKE_CFG_INTDIR");
  if(cfgid && strcmp(cfgid, ".") != 0)
    {
    location += "/";
    location += cfgid;
    location += "/";
    }

  if(this->IsAppBundleOnApple())
    {
    std::string macdir = this->BuildMacContentDirectory("", "", false);
    if(!macdir.empty())
      {
      location += "/";
      location += macdir;
      }
    }
  location += "/";
  location += this->GetFullName("", false);
  return location.c_str();
}

//----------------------------------------------------------------------------
void cmTarget::GetTargetVersion(int& major, int& minor) const
{
  int patch;
  this->GetTargetVersion(false, major, minor, patch);
}

//----------------------------------------------------------------------------
void cmTarget::GetTargetVersion(bool soversion,
                                int& major, int& minor, int& patch) const
{
  // Set the default values.
  major = 0;
  minor = 0;
  patch = 0;

  assert(this->GetType() != INTERFACE_LIBRARY);

  // Look for a VERSION or SOVERSION property.
  const char* prop = soversion? "SOVERSION" : "VERSION";
  if(const char* version = this->GetProperty(prop))
    {
    // Try to parse the version number and store the results that were
    // successfully parsed.
    int parsed_major;
    int parsed_minor;
    int parsed_patch;
    switch(sscanf(version, "%d.%d.%d",
                  &parsed_major, &parsed_minor, &parsed_patch))
      {
      case 3: patch = parsed_patch; // no break!
      case 2: minor = parsed_minor; // no break!
      case 1: major = parsed_major; // no break!
      default: break;
      }
    }
}

//----------------------------------------------------------------------------
const char* cmTarget::GetFeature(const std::string& feature,
                                 const std::string& config) const
{
  if(!config.empty())
    {
    std::string featureConfig = feature;
    featureConfig += "_";
    featureConfig += cmSystemTools::UpperCase(config);
    if(const char* value = this->GetProperty(featureConfig))
      {
      return value;
      }
    }
  if(const char* value = this->GetProperty(feature))
    {
    return value;
    }
  return this->Makefile->GetFeature(feature, config);
}

//----------------------------------------------------------------------------
bool cmTarget::GetFeatureAsBool(const std::string& feature,
                                const std::string& config) const
{
  return cmSystemTools::IsOn(this->GetFeature(feature, config));
}

//----------------------------------------------------------------------------
bool cmTarget::HandleLocationPropertyPolicy(cmMakefile* context) const
{
  if (this->IsImported())
    {
    return true;
    }
  cmOStringStream e;
  const char *modal = 0;
  cmake::MessageType messageType = cmake::AUTHOR_WARNING;
  switch (context->GetPolicyStatus(cmPolicies::CMP0026))
    {
    case cmPolicies::WARN:
      e << (this->Makefile->GetPolicies()
        ->GetPolicyWarning(cmPolicies::CMP0026)) << "\n";
      modal = "should";
    case cmPolicies::OLD:
      break;
    case cmPolicies::REQUIRED_ALWAYS:
    case cmPolicies::REQUIRED_IF_USED:
    case cmPolicies::NEW:
      modal = "may";
      messageType = cmake::FATAL_ERROR;
    }

  if (modal)
    {
    e << "The LOCATION property " << modal << " not be read from target \""
      << this->GetName() << "\".  Use the target name directly with "
      "add_custom_command, or use the generator expression $<TARGET_FILE>, "
      "as appropriate.\n";
    context->IssueMessage(messageType, e.str());
    }

  return messageType != cmake::FATAL_ERROR;
}

//----------------------------------------------------------------------------
const char *cmTarget::GetProperty(const std::string& prop) const
{
  return this->GetProperty(prop, this->Makefile);
}

//----------------------------------------------------------------------------
const char *cmTarget::GetProperty(const std::string& prop,
                                  cmMakefile* context) const
{
  if (this->GetType() == INTERFACE_LIBRARY
      && !whiteListedInterfaceProperty(prop))
    {
    cmOStringStream e;
    e << "INTERFACE_LIBRARY targets may only have whitelisted properties.  "
         "The property \"" << prop << "\" is not allowed.";
    context->IssueMessage(cmake::FATAL_ERROR, e.str());
    return 0;
    }

  if (prop == "NAME")
    {
    return this->GetName().c_str();
    }

  // Watch for special "computed" properties that are dependent on
  // other properties or variables.  Always recompute them.
  if(this->GetType() == cmTarget::EXECUTABLE ||
     this->GetType() == cmTarget::STATIC_LIBRARY ||
     this->GetType() == cmTarget::SHARED_LIBRARY ||
     this->GetType() == cmTarget::MODULE_LIBRARY ||
     this->GetType() == cmTarget::UNKNOWN_LIBRARY)
    {
    if(prop == "LOCATION")
      {
      if (!this->HandleLocationPropertyPolicy(context))
        {
        return 0;
        }

      // Set the LOCATION property of the target.
      //
      // For an imported target this is the location of an arbitrary
      // available configuration.
      //
      // For a non-imported target this is deprecated because it
      // cannot take into account the per-configuration name of the
      // target because the configuration type may not be known at
      // CMake time.
      this->Properties.SetProperty("LOCATION", this->GetLocationForBuild(),
                                   cmProperty::TARGET);
      }

    // Support "LOCATION_<CONFIG>".
    if(cmHasLiteralPrefix(prop, "LOCATION_"))
      {
      if (!this->HandleLocationPropertyPolicy(context))
        {
        return 0;
        }
      const char* configName = prop.c_str() + 9;
      this->Properties.SetProperty(prop,
                                   this->GetLocation(configName),
                                   cmProperty::TARGET);
      }
    // Support "<CONFIG>_LOCATION".
    if(cmHasLiteralSuffix(prop, "_LOCATION"))
      {
      std::string configName(prop.c_str(), prop.size() - 9);
      if(configName != "IMPORTED")
        {
        if (!this->HandleLocationPropertyPolicy(context))
          {
          return 0;
          }
        this->Properties.SetProperty(prop,
                                     this->GetLocation(configName),
                                     cmProperty::TARGET);
        }
      }
    }
  if(prop == "INCLUDE_DIRECTORIES")
    {
    static std::string output;
    output = "";
    std::string sep;
    typedef cmTargetInternals::TargetPropertyEntry
                                TargetPropertyEntry;
    for (std::vector<TargetPropertyEntry*>::const_iterator
        it = this->Internal->IncludeDirectoriesEntries.begin(),
        end = this->Internal->IncludeDirectoriesEntries.end();
        it != end; ++it)
      {
      output += sep;
      output += (*it)->ge->GetInput();
      sep = ";";
      }
    return output.c_str();
    }
  if(prop == "COMPILE_OPTIONS")
    {
    static std::string output;
    output = "";
    std::string sep;
    typedef cmTargetInternals::TargetPropertyEntry
                                TargetPropertyEntry;
    for (std::vector<TargetPropertyEntry*>::const_iterator
        it = this->Internal->CompileOptionsEntries.begin(),
        end = this->Internal->CompileOptionsEntries.end();
        it != end; ++it)
      {
      output += sep;
      output += (*it)->ge->GetInput();
      sep = ";";
      }
    return output.c_str();
    }
  if(prop == "COMPILE_FEATURES")
    {
    static std::string output;
    output = "";
    std::string sep;
    typedef cmTargetInternals::TargetPropertyEntry
                                TargetPropertyEntry;
    for (std::vector<TargetPropertyEntry*>::const_iterator
        it = this->Internal->CompileFeaturesEntries.begin(),
        end = this->Internal->CompileFeaturesEntries.end();
        it != end; ++it)
      {
      output += sep;
      output += (*it)->ge->GetInput();
      sep = ";";
      }
    return output.c_str();
    }
  if(prop == "COMPILE_DEFINITIONS")
    {
    static std::string output;
    output = "";
    std::string sep;
    typedef cmTargetInternals::TargetPropertyEntry
                                TargetPropertyEntry;
    for (std::vector<TargetPropertyEntry*>::const_iterator
        it = this->Internal->CompileDefinitionsEntries.begin(),
        end = this->Internal->CompileDefinitionsEntries.end();
        it != end; ++it)
      {
      output += sep;
      output += (*it)->ge->GetInput();
      sep = ";";
      }
    return output.c_str();
    }
  if(prop == "LINK_LIBRARIES")
    {
    static std::string output;
    output = "";
    std::string sep;
    for (std::vector<cmValueWithOrigin>::const_iterator
        it = this->Internal->LinkImplementationPropertyEntries.begin(),
        end = this->Internal->LinkImplementationPropertyEntries.end();
        it != end; ++it)
      {
      output += sep;
      output += it->Value;
      sep = ";";
      }
    return output.c_str();
    }

  if (prop == "IMPORTED")
    {
    return this->IsImported()?"TRUE":"FALSE";
    }

  if(prop == "SOURCES")
    {
    cmOStringStream ss;
    const char* sep = "";
    typedef cmTargetInternals::TargetPropertyEntry
                                TargetPropertyEntry;
    for(std::vector<TargetPropertyEntry*>::const_iterator
          i = this->Internal->SourceEntries.begin();
        i != this->Internal->SourceEntries.end(); ++i)
      {
      std::string entry = (*i)->ge->GetInput();

      std::vector<std::string> files;
      cmSystemTools::ExpandListArgument(entry, files);
      for (std::vector<std::string>::const_iterator
          li = files.begin(); li != files.end(); ++li)
        {
        if(cmHasLiteralPrefix(*li, "$<TARGET_OBJECTS:") &&
            (*li)[li->size() - 1] == '>')
          {
          std::string objLibName = li->substr(17, li->size()-18);

          if (cmGeneratorExpression::Find(objLibName) != std::string::npos)
            {
            ss << sep;
            sep = ";";
            ss << *li;
            continue;
            }

          bool addContent = false;
          bool noMessage = true;
          cmOStringStream e;
          cmake::MessageType messageType = cmake::AUTHOR_WARNING;
          switch(context->GetPolicyStatus(cmPolicies::CMP0051))
            {
            case cmPolicies::WARN:
              e << (this->Makefile->GetPolicies()
                    ->GetPolicyWarning(cmPolicies::CMP0051)) << "\n";
              noMessage = false;
            case cmPolicies::OLD:
              break;
            case cmPolicies::REQUIRED_ALWAYS:
            case cmPolicies::REQUIRED_IF_USED:
            case cmPolicies::NEW:
              addContent = true;
            }
          if (!noMessage)
            {
            e << "Target \"" << this->Name << "\" contains $<TARGET_OBJECTS> "
            "generator expression in its sources list.  This content was not "
            "previously part of the SOURCES property when that property was "
            "read at configure time.  Code reading that property needs to be "
            "adapted to ignore the generator expression using the "
            "string(GENEX_STRIP) command.";
            context->IssueMessage(messageType, e.str());
            }
          if (addContent)
            {
            ss << sep;
            sep = ";";
            ss << *li;
            }
          }
        else if (cmGeneratorExpression::Find(*li) == std::string::npos)
          {
          ss << sep;
          sep = ";";
          ss << *li;
          }
        else
          {
          cmSourceFile *sf = this->Makefile->GetOrCreateSource(*li);
          // Construct what is known about this source file location.
          cmSourceFileLocation const& location = sf->GetLocation();
          std::string sname = location.GetDirectory();
          if(!sname.empty())
            {
            sname += "/";
            }
          sname += location.GetName();

          ss << sep;
          sep = ";";
          // Append this list entry.
          ss << sname;
          }
        }
      }
    this->Properties.SetProperty("SOURCES", ss.str().c_str(),
                                 cmProperty::TARGET);
    }

  // the type property returns what type the target is
  if (prop == "TYPE")
    {
    return cmTarget::GetTargetTypeName(this->GetType());
    }
  bool chain = false;
  const char *retVal =
    this->Properties.GetPropertyValue(prop, cmProperty::TARGET, chain);
  if (chain)
    {
    return this->Makefile->GetProperty(prop, cmProperty::TARGET);
    }
  return retVal;
}

//----------------------------------------------------------------------------
bool cmTarget::GetPropertyAsBool(const std::string& prop) const
{
  return cmSystemTools::IsOn(this->GetProperty(prop));
}

//----------------------------------------------------------------------------
class cmTargetCollectLinkLanguages
{
public:
  cmTargetCollectLinkLanguages(cmTarget const* target,
                               const std::string& config,
                               std::set<std::string>& languages,
                               cmTarget const* head):
    Config(config), Languages(languages), HeadTarget(head),
    Makefile(target->GetMakefile()), Target(target)
  { this->Visited.insert(target); }

  void Visit(cmLinkItem const& item)
    {
    if(!item.Target)
      {
      if(item.find("::") != std::string::npos)
        {
        bool noMessage = false;
        cmake::MessageType messageType = cmake::FATAL_ERROR;
        cmOStringStream e;
        switch(this->Makefile->GetPolicyStatus(cmPolicies::CMP0028))
          {
          case cmPolicies::WARN:
            {
            e << (this->Makefile->GetPolicies()
                  ->GetPolicyWarning(cmPolicies::CMP0028)) << "\n";
            messageType = cmake::AUTHOR_WARNING;
            }
            break;
          case cmPolicies::OLD:
            noMessage = true;
          case cmPolicies::REQUIRED_IF_USED:
          case cmPolicies::REQUIRED_ALWAYS:
          case cmPolicies::NEW:
            // Issue the fatal message.
            break;
          }

        if(!noMessage)
          {
          e << "Target \"" << this->Target->GetName()
            << "\" links to target \"" << item
            << "\" but the target was not found.  Perhaps a find_package() "
            "call is missing for an IMPORTED target, or an ALIAS target is "
            "missing?";
          this->Makefile->GetCMakeInstance()->IssueMessage(messageType,
                                                e.str(),
                                                this->Target->GetBacktrace());
          }
        }
      return;
      }
    if(!this->Visited.insert(item.Target).second)
      {
      return;
      }

    cmTarget::LinkInterface const* iface =
      item.Target->GetLinkInterface(this->Config, this->HeadTarget);
    if(!iface) { return; }

    for(std::vector<std::string>::const_iterator
          li = iface->Languages.begin(); li != iface->Languages.end(); ++li)
      {
      this->Languages.insert(*li);
      }

    for(std::vector<cmLinkItem>::const_iterator
          li = iface->Libraries.begin(); li != iface->Libraries.end(); ++li)
      {
      this->Visit(*li);
      }
    }
private:
  std::string Config;
  std::set<std::string>& Languages;
  cmTarget const* HeadTarget;
  cmMakefile* Makefile;
  const cmTarget* Target;
  std::set<cmTarget const*> Visited;
};

//----------------------------------------------------------------------------
std::string cmTarget::GetLinkerLanguage(const std::string& config) const
{
  return this->GetLinkClosure(config)->LinkerLanguage;
}

//----------------------------------------------------------------------------
cmTarget::LinkClosure const*
cmTarget::GetLinkClosure(const std::string& config) const
{
  std::string key(cmSystemTools::UpperCase(config));
  cmTargetInternals::LinkClosureMapType::iterator
    i = this->Internal->LinkClosureMap.find(key);
  if(i == this->Internal->LinkClosureMap.end())
    {
    LinkClosure lc;
    this->ComputeLinkClosure(config, lc);
    cmTargetInternals::LinkClosureMapType::value_type entry(key, lc);
    i = this->Internal->LinkClosureMap.insert(entry).first;
    }
  return &i->second;
}

//----------------------------------------------------------------------------
class cmTargetSelectLinker
{
  int Preference;
  cmTarget const* Target;
  cmMakefile* Makefile;
  cmGlobalGenerator* GG;
  std::set<std::string> Preferred;
public:
  cmTargetSelectLinker(cmTarget const* target): Preference(0), Target(target)
    {
    this->Makefile = this->Target->GetMakefile();
    this->GG = this->Makefile->GetLocalGenerator()->GetGlobalGenerator();
    }
  void Consider(const std::string& lang)
    {
    int preference = this->GG->GetLinkerPreference(lang);
    if(preference > this->Preference)
      {
      this->Preference = preference;
      this->Preferred.clear();
      }
    if(preference == this->Preference)
      {
      this->Preferred.insert(lang);
      }
    }
  std::string Choose()
    {
    if(this->Preferred.empty())
      {
      return "";
      }
    else if(this->Preferred.size() > 1)
      {
      cmOStringStream e;
      e << "Target " << this->Target->GetName()
        << " contains multiple languages with the highest linker preference"
        << " (" << this->Preference << "):\n";
      for(std::set<std::string>::const_iterator
            li = this->Preferred.begin(); li != this->Preferred.end(); ++li)
        {
        e << "  " << *li << "\n";
        }
      e << "Set the LINKER_LANGUAGE property for this target.";
      cmake* cm = this->Makefile->GetCMakeInstance();
      cm->IssueMessage(cmake::FATAL_ERROR, e.str(),
                       this->Target->GetBacktrace());
      }
    return *this->Preferred.begin();
    }
};

//----------------------------------------------------------------------------
void cmTarget::ComputeLinkClosure(const std::string& config,
                                  LinkClosure& lc) const
{
  // Get languages built in this target.
  std::set<std::string> languages;
  LinkImplementation const* impl = this->GetLinkImplementation(config);
  for(std::vector<std::string>::const_iterator li = impl->Languages.begin();
      li != impl->Languages.end(); ++li)
    {
    languages.insert(*li);
    }

  // Add interface languages from linked targets.
  cmTargetCollectLinkLanguages cll(this, config, languages, this);
  for(std::vector<cmLinkItem>::const_iterator li = impl->Libraries.begin();
      li != impl->Libraries.end(); ++li)
    {
    cll.Visit(*li);
    }

  // Store the transitive closure of languages.
  for(std::set<std::string>::const_iterator li = languages.begin();
      li != languages.end(); ++li)
    {
    lc.Languages.push_back(*li);
    }

  // Choose the language whose linker should be used.
  if(this->GetProperty("HAS_CXX"))
    {
    lc.LinkerLanguage = "CXX";
    }
  else if(const char* linkerLang = this->GetProperty("LINKER_LANGUAGE"))
    {
    lc.LinkerLanguage = linkerLang;
    }
  else
    {
    // Find the language with the highest preference value.
    cmTargetSelectLinker tsl(this);

    // First select from the languages compiled directly in this target.
    for(std::vector<std::string>::const_iterator li = impl->Languages.begin();
        li != impl->Languages.end(); ++li)
      {
      tsl.Consider(*li);
      }

    // Now consider languages that propagate from linked targets.
    for(std::set<std::string>::const_iterator sit = languages.begin();
        sit != languages.end(); ++sit)
      {
      std::string propagates = "CMAKE_"+*sit+"_LINKER_PREFERENCE_PROPAGATES";
      if(this->Makefile->IsOn(propagates))
        {
        tsl.Consider(*sit);
        }
      }

    lc.LinkerLanguage = tsl.Choose();
    }
}

//----------------------------------------------------------------------------
void cmTarget::ExpandLinkItems(std::string const& prop,
                               std::string const& value,
                               std::string const& config,
                               cmTarget const* headTarget,
                               bool usage_requirements_only,
                               std::vector<cmLinkItem>& items) const
{
  cmGeneratorExpression ge;
  cmGeneratorExpressionDAGChecker dagChecker(this->GetName(), prop, 0, 0);
  if(usage_requirements_only)
    {
    dagChecker.SetTransitivePropertiesOnly();
    }
  std::vector<std::string> libs;
  cmSystemTools::ExpandListArgument(ge.Parse(value)->Evaluate(
                                      this->Makefile,
                                      config,
                                      false,
                                      headTarget,
                                      this, &dagChecker), libs);
  this->LookupLinkItems(libs, items);
}

//----------------------------------------------------------------------------
void cmTarget::LookupLinkItems(std::vector<std::string> const& names,
                               std::vector<cmLinkItem>& items) const
{
  for(std::vector<std::string>::const_iterator i = names.begin();
      i != names.end(); ++i)
    {
    std::string name = this->CheckCMP0004(*i);
    if(name == this->GetName() || name.empty())
      {
      continue;
      }
    items.push_back(cmLinkItem(name, this->FindTargetToLink(name)));
    }
}

//----------------------------------------------------------------------------
const char* cmTarget::GetSuffixVariableInternal(bool implib) const
{
  switch(this->GetType())
    {
    case cmTarget::STATIC_LIBRARY:
      return "CMAKE_STATIC_LIBRARY_SUFFIX";
    case cmTarget::SHARED_LIBRARY:
      return (implib
              ? "CMAKE_IMPORT_LIBRARY_SUFFIX"
              : "CMAKE_SHARED_LIBRARY_SUFFIX");
    case cmTarget::MODULE_LIBRARY:
      return (implib
              ? "CMAKE_IMPORT_LIBRARY_SUFFIX"
              : "CMAKE_SHARED_MODULE_SUFFIX");
    case cmTarget::EXECUTABLE:
      return (implib
              ? "CMAKE_IMPORT_LIBRARY_SUFFIX"
              : "CMAKE_EXECUTABLE_SUFFIX");
    default:
      break;
    }
  return "";
}


//----------------------------------------------------------------------------
const char* cmTarget::GetPrefixVariableInternal(bool implib) const
{
  switch(this->GetType())
    {
    case cmTarget::STATIC_LIBRARY:
      return "CMAKE_STATIC_LIBRARY_PREFIX";
    case cmTarget::SHARED_LIBRARY:
      return (implib
              ? "CMAKE_IMPORT_LIBRARY_PREFIX"
              : "CMAKE_SHARED_LIBRARY_PREFIX");
    case cmTarget::MODULE_LIBRARY:
      return (implib
              ? "CMAKE_IMPORT_LIBRARY_PREFIX"
              : "CMAKE_SHARED_MODULE_PREFIX");
    case cmTarget::EXECUTABLE:
      return (implib? "CMAKE_IMPORT_LIBRARY_PREFIX" : "");
    default:
      break;
    }
  return "";
}

//----------------------------------------------------------------------------
std::string cmTarget::GetPDBName(const std::string& config) const
{
  std::string prefix;
  std::string base;
  std::string suffix;
  this->GetFullNameInternal(config, false, prefix, base, suffix);

  std::vector<std::string> props;
  std::string configUpper = cmSystemTools::UpperCase(config);
  if(!configUpper.empty())
    {
    // PDB_NAME_<CONFIG>
    props.push_back("PDB_NAME_" + configUpper);
    }

  // PDB_NAME
  props.push_back("PDB_NAME");

  for(std::vector<std::string>::const_iterator i = props.begin();
      i != props.end(); ++i)
    {
    if(const char* outName = this->GetProperty(*i))
      {
      base = outName;
      break;
      }
    }
  return prefix+base+".pdb";
}

//----------------------------------------------------------------------------
std::string cmTarget::GetCompilePDBName(const std::string& config) const
{
  std::string prefix;
  std::string base;
  std::string suffix;
  this->GetFullNameInternal(config, false, prefix, base, suffix);

  // Check for a per-configuration output directory target property.
  std::string configUpper = cmSystemTools::UpperCase(config);
  std::string configProp = "COMPILE_PDB_NAME_";
  configProp += configUpper;
  const char* config_name = this->GetProperty(configProp);
  if(config_name && *config_name)
    {
    return prefix + config_name + ".pdb";
    }

  const char* name = this->GetProperty("COMPILE_PDB_NAME");
  if(name && *name)
    {
    return prefix + name + ".pdb";
    }

  return "";
}

//----------------------------------------------------------------------------
std::string cmTarget::GetCompilePDBPath(const std::string& config) const
{
  std::string dir = this->GetCompilePDBDirectory(config);
  std::string name = this->GetCompilePDBName(config);
  if(dir.empty() && !name.empty())
    {
    dir = this->GetPDBDirectory(config);
    }
  if(!dir.empty())
    {
    dir += "/";
    }
  return dir + name;
}

//----------------------------------------------------------------------------
bool cmTarget::HasSOName(const std::string& config) const
{
  // soname is supported only for shared libraries and modules,
  // and then only when the platform supports an soname flag.
  return ((this->GetType() == cmTarget::SHARED_LIBRARY ||
           this->GetType() == cmTarget::MODULE_LIBRARY) &&
          !this->GetPropertyAsBool("NO_SONAME") &&
          this->Makefile->GetSONameFlag(this->GetLinkerLanguage(config)));
}

//----------------------------------------------------------------------------
std::string cmTarget::GetSOName(const std::string& config) const
{
  if(this->IsImported())
    {
    // Lookup the imported soname.
    if(cmTarget::ImportInfo const* info = this->GetImportInfo(config))
      {
      if(info->NoSOName)
        {
        // The imported library has no builtin soname so the name
        // searched at runtime will be just the filename.
        return cmSystemTools::GetFilenameName(info->Location);
        }
      else
        {
        // Use the soname given if any.
        if(info->SOName.find("@rpath/") == 0)
          {
          return info->SOName.substr(6);
          }
        return info->SOName;
        }
      }
    else
      {
      return "";
      }
    }
  else
    {
    // Compute the soname that will be built.
    std::string name;
    std::string soName;
    std::string realName;
    std::string impName;
    std::string pdbName;
    this->GetLibraryNames(name, soName, realName, impName, pdbName, config);
    return soName;
    }
}

//----------------------------------------------------------------------------
bool cmTarget::HasMacOSXRpathInstallNameDir(const std::string& config) const
{
  bool install_name_is_rpath = false;
  bool macosx_rpath = false;

  if(!this->IsImportedTarget)
    {
    if(this->GetType() != cmTarget::SHARED_LIBRARY)
      {
      return false;
      }
    const char* install_name = this->GetProperty("INSTALL_NAME_DIR");
    bool use_install_name =
      this->GetPropertyAsBool("BUILD_WITH_INSTALL_RPATH");
    if(install_name && use_install_name &&
       std::string(install_name) == "@rpath")
      {
      install_name_is_rpath = true;
      }
    else if(install_name && use_install_name)
      {
      return false;
      }
    if(!install_name_is_rpath)
      {
      macosx_rpath = this->MacOSXRpathInstallNameDirDefault();
      }
    }
  else
    {
    // Lookup the imported soname.
    if(cmTarget::ImportInfo const* info = this->GetImportInfo(config))
      {
      if(!info->NoSOName && !info->SOName.empty())
        {
        if(info->SOName.find("@rpath/") == 0)
          {
          install_name_is_rpath = true;
          }
        }
      else
        {
        std::string install_name;
        cmSystemTools::GuessLibraryInstallName(info->Location, install_name);
        if(install_name.find("@rpath") != std::string::npos)
          {
          install_name_is_rpath = true;
          }
        }
      }
    }

  if(!install_name_is_rpath && !macosx_rpath)
    {
    return false;
    }

  if(!this->Makefile->IsSet("CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG"))
    {
    cmOStringStream w;
    w << "Attempting to use";
    if(macosx_rpath)
      {
      w << " MACOSX_RPATH";
      }
    else
      {
      w << " @rpath";
      }
    w << " without CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG being set.";
    w << "  This could be because you are using a Mac OS X version";
    w << " less than 10.5 or because CMake's platform configuration is";
    w << " corrupt.";
    cmake* cm = this->Makefile->GetCMakeInstance();
    cm->IssueMessage(cmake::FATAL_ERROR, w.str(), this->GetBacktrace());
    }

  return true;
}

//----------------------------------------------------------------------------
bool cmTarget::MacOSXRpathInstallNameDirDefault() const
{
  // we can't do rpaths when unsupported
  if(!this->Makefile->IsSet("CMAKE_SHARED_LIBRARY_RUNTIME_C_FLAG"))
    {
    return false;
    }

  const char* macosx_rpath_str = this->GetProperty("MACOSX_RPATH");
  if(macosx_rpath_str)
    {
    return this->GetPropertyAsBool("MACOSX_RPATH");
    }

  cmPolicies::PolicyStatus cmp0042 = this->GetPolicyStatusCMP0042();

  if(cmp0042 == cmPolicies::WARN)
    {
    this->Makefile->GetLocalGenerator()->GetGlobalGenerator()->
      AddCMP0042WarnTarget(this->GetName());
    }

  if(cmp0042 == cmPolicies::NEW)
    {
    return true;
    }

  return false;
}

//----------------------------------------------------------------------------
bool cmTarget::IsImportedSharedLibWithoutSOName(
                                          const std::string& config) const
{
  if(this->IsImported() && this->GetType() == cmTarget::SHARED_LIBRARY)
    {
    if(cmTarget::ImportInfo const* info = this->GetImportInfo(config))
      {
      return info->NoSOName;
      }
    }
  return false;
}

//----------------------------------------------------------------------------
std::string cmTarget::NormalGetRealName(const std::string& config) const
{
  // This should not be called for imported targets.
  // TODO: Split cmTarget into a class hierarchy to get compile-time
  // enforcement of the limited imported target API.
  if(this->IsImported())
    {
    std::string msg =  "NormalGetRealName called on imported target: ";
    msg += this->GetName();
    this->GetMakefile()->
      IssueMessage(cmake::INTERNAL_ERROR,
                   msg);
    }

  if(this->GetType() == cmTarget::EXECUTABLE)
    {
    // Compute the real name that will be built.
    std::string name;
    std::string realName;
    std::string impName;
    std::string pdbName;
    this->GetExecutableNames(name, realName, impName, pdbName, config);
    return realName;
    }
  else
    {
    // Compute the real name that will be built.
    std::string name;
    std::string soName;
    std::string realName;
    std::string impName;
    std::string pdbName;
    this->GetLibraryNames(name, soName, realName, impName, pdbName, config);
    return realName;
    }
}

//----------------------------------------------------------------------------
std::string cmTarget::GetFullName(const std::string& config,
                                  bool implib) const
{
  if(this->IsImported())
    {
    return this->GetFullNameImported(config, implib);
    }
  else
    {
    return this->GetFullNameInternal(config, implib);
    }
}

//----------------------------------------------------------------------------
std::string
cmTarget::GetFullNameImported(const std::string& config, bool implib) const
{
  return cmSystemTools::GetFilenameName(
    this->ImportedGetFullPath(config, implib));
}

//----------------------------------------------------------------------------
void cmTarget::GetFullNameComponents(std::string& prefix, std::string& base,
                                     std::string& suffix,
                                     const std::string& config,
                                     bool implib) const
{
  this->GetFullNameInternal(config, implib, prefix, base, suffix);
}

//----------------------------------------------------------------------------
std::string cmTarget::GetFullPath(const std::string& config, bool implib,
                                  bool realname) const
{
  if(this->IsImported())
    {
    return this->ImportedGetFullPath(config, implib);
    }
  else
    {
    return this->NormalGetFullPath(config, implib, realname);
    }
}

//----------------------------------------------------------------------------
std::string cmTarget::NormalGetFullPath(const std::string& config,
                                        bool implib, bool realname) const
{
  std::string fpath = this->GetDirectory(config, implib);
  fpath += "/";
  if(this->IsAppBundleOnApple())
    {
    fpath = this->BuildMacContentDirectory(fpath, config, false);
    fpath += "/";
    }

  // Add the full name of the target.
  if(implib)
    {
    fpath += this->GetFullName(config, true);
    }
  else if(realname)
    {
    fpath += this->NormalGetRealName(config);
    }
  else
    {
    fpath += this->GetFullName(config, false);
    }
  return fpath;
}

//----------------------------------------------------------------------------
std::string
cmTarget::ImportedGetFullPath(const std::string& config, bool implib) const
{
  std::string result;
  if(cmTarget::ImportInfo const* info = this->GetImportInfo(config))
    {
    result = implib? info->ImportLibrary : info->Location;
    }
  if(result.empty())
    {
    result = this->GetName();
    result += "-NOTFOUND";
    }
  return result;
}

//----------------------------------------------------------------------------
std::string
cmTarget::GetFullNameInternal(const std::string& config, bool implib) const
{
  std::string prefix;
  std::string base;
  std::string suffix;
  this->GetFullNameInternal(config, implib, prefix, base, suffix);
  return prefix+base+suffix;
}

//----------------------------------------------------------------------------
void cmTarget::GetFullNameInternal(const std::string& config,
                                   bool implib,
                                   std::string& outPrefix,
                                   std::string& outBase,
                                   std::string& outSuffix) const
{
  // Use just the target name for non-main target types.
  if(this->GetType() != cmTarget::STATIC_LIBRARY &&
     this->GetType() != cmTarget::SHARED_LIBRARY &&
     this->GetType() != cmTarget::MODULE_LIBRARY &&
     this->GetType() != cmTarget::EXECUTABLE)
    {
    outPrefix = "";
    outBase = this->GetName();
    outSuffix = "";
    return;
    }

  // Return an empty name for the import library if this platform
  // does not support import libraries.
  if(implib &&
     !this->Makefile->GetDefinition("CMAKE_IMPORT_LIBRARY_SUFFIX"))
    {
    outPrefix = "";
    outBase = "";
    outSuffix = "";
    return;
    }

  // The implib option is only allowed for shared libraries, module
  // libraries, and executables.
  if(this->GetType() != cmTarget::SHARED_LIBRARY &&
     this->GetType() != cmTarget::MODULE_LIBRARY &&
     this->GetType() != cmTarget::EXECUTABLE)
    {
    implib = false;
    }

  // Compute the full name for main target types.
  const char* targetPrefix = (implib
                              ? this->GetProperty("IMPORT_PREFIX")
                              : this->GetProperty("PREFIX"));
  const char* targetSuffix = (implib
                              ? this->GetProperty("IMPORT_SUFFIX")
                              : this->GetProperty("SUFFIX"));
  const char* configPostfix = 0;
  if(!config.empty())
    {
    std::string configProp = cmSystemTools::UpperCase(config);
    configProp += "_POSTFIX";
    configPostfix = this->GetProperty(configProp);
    // Mac application bundles and frameworks have no postfix.
    if(configPostfix &&
       (this->IsAppBundleOnApple() || this->IsFrameworkOnApple()))
      {
      configPostfix = 0;
      }
    }
  const char* prefixVar = this->GetPrefixVariableInternal(implib);
  const char* suffixVar = this->GetSuffixVariableInternal(implib);

  // Check for language-specific default prefix and suffix.
  std::string ll = this->GetLinkerLanguage(config);
  if(!ll.empty())
    {
    if(!targetSuffix && suffixVar && *suffixVar)
      {
      std::string langSuff = suffixVar + std::string("_") + ll;
      targetSuffix = this->Makefile->GetDefinition(langSuff);
      }
    if(!targetPrefix && prefixVar && *prefixVar)
      {
      std::string langPrefix = prefixVar + std::string("_") + ll;
      targetPrefix = this->Makefile->GetDefinition(langPrefix);
      }
    }

  // if there is no prefix on the target use the cmake definition
  if(!targetPrefix && prefixVar)
    {
    targetPrefix = this->Makefile->GetSafeDefinition(prefixVar);
    }
  // if there is no suffix on the target use the cmake definition
  if(!targetSuffix && suffixVar)
    {
    targetSuffix = this->Makefile->GetSafeDefinition(suffixVar);
    }

  // frameworks have directory prefix but no suffix
  std::string fw_prefix;
  if(this->IsFrameworkOnApple())
    {
    fw_prefix = this->GetOutputName(config, false);
    fw_prefix += ".framework/";
    targetPrefix = fw_prefix.c_str();
    targetSuffix = 0;
    }

  if(this->IsCFBundleOnApple())
    {
    fw_prefix = this->GetOutputName(config, false);
    fw_prefix += ".";
    const char *ext = this->GetProperty("BUNDLE_EXTENSION");
    if (!ext)
      {
      ext = "bundle";
      }
    fw_prefix += ext;
    fw_prefix += "/Contents/MacOS/";
    targetPrefix = fw_prefix.c_str();
    targetSuffix = 0;
    }

  // Begin the final name with the prefix.
  outPrefix = targetPrefix?targetPrefix:"";

  // Append the target name or property-specified name.
  outBase += this->GetOutputName(config, implib);

  // Append the per-configuration postfix.
  outBase += configPostfix?configPostfix:"";

  // Name shared libraries with their version number on some platforms.
  if(const char* soversion = this->GetProperty("SOVERSION"))
    {
    if(this->GetType() == cmTarget::SHARED_LIBRARY && !implib &&
       this->Makefile->IsOn("CMAKE_SHARED_LIBRARY_NAME_WITH_VERSION"))
      {
      outBase += "-";
      outBase += soversion;
      }
    }

  // Append the suffix.
  outSuffix = targetSuffix?targetSuffix:"";
}

//----------------------------------------------------------------------------
void cmTarget::GetLibraryNames(std::string& name,
                               std::string& soName,
                               std::string& realName,
                               std::string& impName,
                               std::string& pdbName,
                               const std::string& config) const
{
  // This should not be called for imported targets.
  // TODO: Split cmTarget into a class hierarchy to get compile-time
  // enforcement of the limited imported target API.
  if(this->IsImported())
    {
    std::string msg =  "GetLibraryNames called on imported target: ";
    msg += this->GetName();
    this->Makefile->IssueMessage(cmake::INTERNAL_ERROR,
                                 msg);
    return;
    }

  assert(this->GetType() != INTERFACE_LIBRARY);

  // Check for library version properties.
  const char* version = this->GetProperty("VERSION");
  const char* soversion = this->GetProperty("SOVERSION");
  if(!this->HasSOName(config) ||
     this->Makefile->IsOn("CMAKE_PLATFORM_NO_VERSIONED_SONAME") ||
     this->IsFrameworkOnApple())
    {
    // Versioning is supported only for shared libraries and modules,
    // and then only when the platform supports an soname flag.
    version = 0;
    soversion = 0;
    }
  if(version && !soversion)
    {
    // The soversion must be set if the library version is set.  Use
    // the library version as the soversion.
    soversion = version;
    }
  if(!version && soversion)
    {
    // Use the soversion as the library version.
    version = soversion;
    }

  // Get the components of the library name.
  std::string prefix;
  std::string base;
  std::string suffix;
  this->GetFullNameInternal(config, false, prefix, base, suffix);

  // The library name.
  name = prefix+base+suffix;

  if(this->IsFrameworkOnApple())
    {
    realName = prefix;
    realName += "Versions/";
    realName += this->GetFrameworkVersion();
    realName += "/";
    realName += base;
    soName = realName;
    }
  else
    {
    // The library's soname.
    this->ComputeVersionedName(soName, prefix, base, suffix,
                               name, soversion);
    // The library's real name on disk.
    this->ComputeVersionedName(realName, prefix, base, suffix,
                               name, version);
    }

  // The import library name.
  if(this->GetType() == cmTarget::SHARED_LIBRARY ||
     this->GetType() == cmTarget::MODULE_LIBRARY)
    {
    impName = this->GetFullNameInternal(config, true);
    }
  else
    {
    impName = "";
    }

  // The program database file name.
  pdbName = this->GetPDBName(config);
}

//----------------------------------------------------------------------------
void cmTarget::ComputeVersionedName(std::string& vName,
                                    std::string const& prefix,
                                    std::string const& base,
                                    std::string const& suffix,
                                    std::string const& name,
                                    const char* version) const
{
  vName = this->IsApple? (prefix+base) : name;
  if(version)
    {
    vName += ".";
    vName += version;
    }
  vName += this->IsApple? suffix : std::string();
}

//----------------------------------------------------------------------------
void cmTarget::GetExecutableNames(std::string& name,
                                  std::string& realName,
                                  std::string& impName,
                                  std::string& pdbName,
                                  const std::string& config) const
{
  // This should not be called for imported targets.
  // TODO: Split cmTarget into a class hierarchy to get compile-time
  // enforcement of the limited imported target API.
  if(this->IsImported())
    {
    std::string msg =
      "GetExecutableNames called on imported target: ";
    msg += this->GetName();
    this->GetMakefile()->IssueMessage(cmake::INTERNAL_ERROR, msg);
    }

  // This versioning is supported only for executables and then only
  // when the platform supports symbolic links.
#if defined(_WIN32) && !defined(__CYGWIN__)
  const char* version = 0;
#else
  // Check for executable version properties.
  const char* version = this->GetProperty("VERSION");
  if(this->GetType() != cmTarget::EXECUTABLE || this->Makefile->IsOn("XCODE"))
    {
    version = 0;
    }
#endif

  // Get the components of the executable name.
  std::string prefix;
  std::string base;
  std::string suffix;
  this->GetFullNameInternal(config, false, prefix, base, suffix);

  // The executable name.
  name = prefix+base+suffix;

  // The executable's real name on disk.
#if defined(__CYGWIN__)
  realName = prefix+base;
#else
  realName = name;
#endif
  if(version)
    {
    realName += "-";
    realName += version;
    }
#if defined(__CYGWIN__)
  realName += suffix;
#endif

  // The import library name.
  impName = this->GetFullNameInternal(config, true);

  // The program database file name.
  pdbName = this->GetPDBName(config);
}

//----------------------------------------------------------------------------
bool cmTarget::HasImplibGNUtoMS() const
{
  return this->HasImportLibrary() && this->GetPropertyAsBool("GNUtoMS");
}

//----------------------------------------------------------------------------
bool cmTarget::GetImplibGNUtoMS(std::string const& gnuName,
                                std::string& out, const char* newExt) const
{
  if(this->HasImplibGNUtoMS() &&
     gnuName.size() > 6 && gnuName.substr(gnuName.size()-6) == ".dll.a")
    {
    out = gnuName.substr(0, gnuName.size()-6);
    out += newExt? newExt : ".lib";
    return true;
    }
  return false;
}

//----------------------------------------------------------------------------
void cmTarget::SetPropertyDefault(const std::string& property,
                                  const char* default_value)
{
  // Compute the name of the variable holding the default value.
  std::string var = "CMAKE_";
  var += property;

  if(const char* value = this->Makefile->GetDefinition(var))
    {
    this->SetProperty(property, value);
    }
  else if(default_value)
    {
    this->SetProperty(property, default_value);
    }
}

//----------------------------------------------------------------------------
bool cmTarget::HaveBuildTreeRPATH(const std::string& config) const
{
  if (this->GetPropertyAsBool("SKIP_BUILD_RPATH"))
    {
    return false;
    }
  if(LinkImplementation const* impl =
     this->GetLinkImplementationLibraries(config))
    {
    return !impl->Libraries.empty();
    }
  return false;
}

//----------------------------------------------------------------------------
bool cmTarget::HaveInstallTreeRPATH() const
{
  const char* install_rpath = this->GetProperty("INSTALL_RPATH");
  return (install_rpath && *install_rpath) &&
          !this->Makefile->IsOn("CMAKE_SKIP_INSTALL_RPATH");
}

//----------------------------------------------------------------------------
bool cmTarget::NeedRelinkBeforeInstall(const std::string& config) const
{
  // Only executables and shared libraries can have an rpath and may
  // need relinking.
  if(this->TargetTypeValue != cmTarget::EXECUTABLE &&
     this->TargetTypeValue != cmTarget::SHARED_LIBRARY &&
     this->TargetTypeValue != cmTarget::MODULE_LIBRARY)
    {
    return false;
    }

  // If there is no install location this target will not be installed
  // and therefore does not need relinking.
  if(!this->GetHaveInstallRule())
    {
    return false;
    }

  // If skipping all rpaths completely then no relinking is needed.
  if(this->Makefile->IsOn("CMAKE_SKIP_RPATH"))
    {
    return false;
    }

  // If building with the install-tree rpath no relinking is needed.
  if(this->GetPropertyAsBool("BUILD_WITH_INSTALL_RPATH"))
    {
    return false;
    }

  // If chrpath is going to be used no relinking is needed.
  if(this->IsChrpathUsed(config))
    {
    return false;
    }

  // Check for rpath support on this platform.
  std::string ll = this->GetLinkerLanguage(config);
  if(!ll.empty())
    {
    std::string flagVar = "CMAKE_SHARED_LIBRARY_RUNTIME_";
    flagVar += ll;
    flagVar += "_FLAG";
    if(!this->Makefile->IsSet(flagVar))
      {
      // There is no rpath support on this platform so nothing needs
      // relinking.
      return false;
      }
    }
  else
    {
    // No linker language is known.  This error will be reported by
    // other code.
    return false;
    }

  // If either a build or install tree rpath is set then the rpath
  // will likely change between the build tree and install tree and
  // this target must be relinked.
  return this->HaveBuildTreeRPATH(config) || this->HaveInstallTreeRPATH();
}

//----------------------------------------------------------------------------
std::string cmTarget::GetInstallNameDirForBuildTree(
    const std::string& config) const
{
  // If building directly for installation then the build tree install_name
  // is the same as the install tree.
  if(this->GetPropertyAsBool("BUILD_WITH_INSTALL_RPATH"))
    {
    return GetInstallNameDirForInstallTree();
    }

  // Use the build tree directory for the target.
  if(this->Makefile->IsOn("CMAKE_PLATFORM_HAS_INSTALLNAME") &&
     !this->Makefile->IsOn("CMAKE_SKIP_RPATH") &&
     !this->GetPropertyAsBool("SKIP_BUILD_RPATH"))
    {
    std::string dir;
    bool macosx_rpath = this->MacOSXRpathInstallNameDirDefault();
    if(macosx_rpath)
      {
      dir = "@rpath";
      }
    else
      {
      dir = this->GetDirectory(config);
      }
    dir += "/";
    return dir;
    }
  else
    {
    return "";
    }
}

//----------------------------------------------------------------------------
std::string cmTarget::GetInstallNameDirForInstallTree() const
{
  if(this->Makefile->IsOn("CMAKE_PLATFORM_HAS_INSTALLNAME"))
    {
    std::string dir;
    const char* install_name_dir = this->GetProperty("INSTALL_NAME_DIR");

    if(!this->Makefile->IsOn("CMAKE_SKIP_RPATH") &&
       !this->Makefile->IsOn("CMAKE_SKIP_INSTALL_RPATH"))
      {
      if(install_name_dir && *install_name_dir)
        {
        dir = install_name_dir;
        dir += "/";
        }
      }
    if(!install_name_dir)
      {
      if(this->MacOSXRpathInstallNameDirDefault())
        {
        dir = "@rpath/";
        }
      }
    return dir;
    }
  else
    {
    return "";
    }
}

//----------------------------------------------------------------------------
const char* cmTarget::GetOutputTargetType(bool implib) const
{
  switch(this->GetType())
    {
    case cmTarget::SHARED_LIBRARY:
      if(this->DLLPlatform)
        {
        if(implib)
          {
          // A DLL import library is treated as an archive target.
          return "ARCHIVE";
          }
        else
          {
          // A DLL shared library is treated as a runtime target.
          return "RUNTIME";
          }
        }
      else
        {
        // For non-DLL platforms shared libraries are treated as
        // library targets.
        return "LIBRARY";
        }
    case cmTarget::STATIC_LIBRARY:
      // Static libraries are always treated as archive targets.
      return "ARCHIVE";
    case cmTarget::MODULE_LIBRARY:
      if(implib)
        {
        // Module libraries are always treated as library targets.
        return "ARCHIVE";
        }
      else
        {
        // Module import libraries are treated as archive targets.
        return "LIBRARY";
        }
    case cmTarget::EXECUTABLE:
      if(implib)
        {
        // Executable import libraries are treated as archive targets.
        return "ARCHIVE";
        }
      else
        {
        // Executables are always treated as runtime targets.
        return "RUNTIME";
        }
    default:
      break;
    }
  return "";
}

//----------------------------------------------------------------------------
bool cmTarget::ComputeOutputDir(const std::string& config,
                                bool implib, std::string& out) const
{
  bool usesDefaultOutputDir = false;
  std::string conf = config;

  // Look for a target property defining the target output directory
  // based on the target type.
  std::string targetTypeName = this->GetOutputTargetType(implib);
  const char* propertyName = 0;
  std::string propertyNameStr = targetTypeName;
  if(!propertyNameStr.empty())
    {
    propertyNameStr += "_OUTPUT_DIRECTORY";
    propertyName = propertyNameStr.c_str();
    }

  // Check for a per-configuration output directory target property.
  std::string configUpper = cmSystemTools::UpperCase(conf);
  const char* configProp = 0;
  std::string configPropStr = targetTypeName;
  if(!configPropStr.empty())
    {
    configPropStr += "_OUTPUT_DIRECTORY_";
    configPropStr += configUpper;
    configProp = configPropStr.c_str();
    }

  // Select an output directory.
  if(const char* config_outdir = this->GetProperty(configProp))
    {
    // Use the user-specified per-configuration output directory.
    out = config_outdir;

    // Skip per-configuration subdirectory.
    conf = "";
    }
  else if(const char* outdir = this->GetProperty(propertyName))
    {
    // Use the user-specified output directory.
    out = outdir;
    }
  else if(this->GetType() == cmTarget::EXECUTABLE)
    {
    // Lookup the output path for executables.
    out = this->Makefile->GetSafeDefinition("EXECUTABLE_OUTPUT_PATH");
    }
  else if(this->GetType() == cmTarget::STATIC_LIBRARY ||
          this->GetType() == cmTarget::SHARED_LIBRARY ||
          this->GetType() == cmTarget::MODULE_LIBRARY)
    {
    // Lookup the output path for libraries.
    out = this->Makefile->GetSafeDefinition("LIBRARY_OUTPUT_PATH");
    }
  if(out.empty())
    {
    // Default to the current output directory.
    usesDefaultOutputDir = true;
    out = ".";
    }

  // Convert the output path to a full path in case it is
  // specified as a relative path.  Treat a relative path as
  // relative to the current output directory for this makefile.
  out = (cmSystemTools::CollapseFullPath
         (out.c_str(), this->Makefile->GetStartOutputDirectory()));

  // The generator may add the configuration's subdirectory.
  if(!conf.empty())
    {
    const char *platforms = this->Makefile->GetDefinition(
      "CMAKE_XCODE_EFFECTIVE_PLATFORMS");
    std::string suffix =
      usesDefaultOutputDir && platforms ? "$(EFFECTIVE_PLATFORM_NAME)" : "";
    this->Makefile->GetLocalGenerator()->GetGlobalGenerator()->
      AppendDirectoryForConfig("/", conf, suffix, out);
    }

  return usesDefaultOutputDir;
}

//----------------------------------------------------------------------------
bool cmTarget::ComputePDBOutputDir(const std::string& kind,
                                   const std::string& config,
                                   std::string& out) const
{
  // Look for a target property defining the target output directory
  // based on the target type.
  const char* propertyName = 0;
  std::string propertyNameStr = kind;
  if(!propertyNameStr.empty())
    {
    propertyNameStr += "_OUTPUT_DIRECTORY";
    propertyName = propertyNameStr.c_str();
    }
  std::string conf = config;

  // Check for a per-configuration output directory target property.
  std::string configUpper = cmSystemTools::UpperCase(conf);
  const char* configProp = 0;
  std::string configPropStr = kind;
  if(!configPropStr.empty())
    {
    configPropStr += "_OUTPUT_DIRECTORY_";
    configPropStr += configUpper;
    configProp = configPropStr.c_str();
    }

  // Select an output directory.
  if(const char* config_outdir = this->GetProperty(configProp))
    {
    // Use the user-specified per-configuration output directory.
    out = config_outdir;

    // Skip per-configuration subdirectory.
    conf = "";
    }
  else if(const char* outdir = this->GetProperty(propertyName))
    {
    // Use the user-specified output directory.
    out = outdir;
    }
  if(out.empty())
    {
    return false;
    }

  // Convert the output path to a full path in case it is
  // specified as a relative path.  Treat a relative path as
  // relative to the current output directory for this makefile.
  out = (cmSystemTools::CollapseFullPath
         (out.c_str(), this->Makefile->GetStartOutputDirectory()));

  // The generator may add the configuration's subdirectory.
  if(!conf.empty())
    {
    this->Makefile->GetLocalGenerator()->GetGlobalGenerator()->
      AppendDirectoryForConfig("/", conf, "", out);
    }
  return true;
}

//----------------------------------------------------------------------------
bool cmTarget::UsesDefaultOutputDir(const std::string& config,
                                    bool implib) const
{
  std::string dir;
  return this->ComputeOutputDir(config, implib, dir);
}

//----------------------------------------------------------------------------
std::string cmTarget::GetOutputName(const std::string& config,
                                    bool implib) const
{
  std::vector<std::string> props;
  std::string type = this->GetOutputTargetType(implib);
  std::string configUpper = cmSystemTools::UpperCase(config);
  if(!type.empty() && !configUpper.empty())
    {
    // <ARCHIVE|LIBRARY|RUNTIME>_OUTPUT_NAME_<CONFIG>
    props.push_back(type + "_OUTPUT_NAME_" + configUpper);
    }
  if(!type.empty())
    {
    // <ARCHIVE|LIBRARY|RUNTIME>_OUTPUT_NAME
    props.push_back(type + "_OUTPUT_NAME");
    }
  if(!configUpper.empty())
    {
    // OUTPUT_NAME_<CONFIG>
    props.push_back("OUTPUT_NAME_" + configUpper);
    // <CONFIG>_OUTPUT_NAME
    props.push_back(configUpper + "_OUTPUT_NAME");
    }
  // OUTPUT_NAME
  props.push_back("OUTPUT_NAME");

  for(std::vector<std::string>::const_iterator i = props.begin();
      i != props.end(); ++i)
    {
    if(const char* outName = this->GetProperty(*i))
      {
      return outName;
      }
    }
  return this->GetName();
}

//----------------------------------------------------------------------------
std::string cmTarget::GetFrameworkVersion() const
{
  assert(this->GetType() != INTERFACE_LIBRARY);

  if(const char* fversion = this->GetProperty("FRAMEWORK_VERSION"))
    {
    return fversion;
    }
  else if(const char* tversion = this->GetProperty("VERSION"))
    {
    return tversion;
    }
  else
    {
    return "A";
    }
}

//----------------------------------------------------------------------------
const char* cmTarget::GetExportMacro() const
{
  // Define the symbol for targets that export symbols.
  if(this->GetType() == cmTarget::SHARED_LIBRARY ||
     this->GetType() == cmTarget::MODULE_LIBRARY ||
     this->IsExecutableWithExports())
    {
    if(const char* custom_export_name = this->GetProperty("DEFINE_SYMBOL"))
      {
      this->ExportMacro = custom_export_name;
      }
    else
      {
      std::string in = this->GetName();
      in += "_EXPORTS";
      this->ExportMacro = cmSystemTools::MakeCindentifier(in.c_str());
      }
    return this->ExportMacro.c_str();
    }
  else
    {
    return 0;
    }
}

//----------------------------------------------------------------------------
bool cmTarget::IsNullImpliedByLinkLibraries(const std::string &p) const
{
  return this->LinkImplicitNullProperties.find(p)
      != this->LinkImplicitNullProperties.end();
}

//----------------------------------------------------------------------------
template<typename PropertyType>
PropertyType getTypedProperty(cmTarget const* tgt, const char *prop,
                              PropertyType *);

//----------------------------------------------------------------------------
template<>
bool getTypedProperty<bool>(cmTarget const* tgt, const char *prop, bool *)
{
  return tgt->GetPropertyAsBool(prop);
}

//----------------------------------------------------------------------------
template<>
const char *getTypedProperty<const char *>(cmTarget const* tgt,
                                           const char *prop,
                                           const char **)
{
  return tgt->GetProperty(prop);
}

enum CompatibleType
{
  BoolType,
  StringType,
  NumberMinType,
  NumberMaxType
};

//----------------------------------------------------------------------------
template<typename PropertyType>
std::pair<bool, PropertyType> consistentProperty(PropertyType lhs,
                                                 PropertyType rhs,
                                                 CompatibleType t);

//----------------------------------------------------------------------------
template<>
std::pair<bool, bool> consistentProperty(bool lhs, bool rhs, CompatibleType)
{
  return std::make_pair(lhs == rhs, lhs);
}

//----------------------------------------------------------------------------
std::pair<bool, const char*> consistentStringProperty(const char *lhs,
                                                      const char *rhs)
{
  const bool b = strcmp(lhs, rhs) == 0;
  return std::make_pair(b, b ? lhs : 0);
}

#if defined(_MSC_VER) && _MSC_VER <= 1200
template<typename T> const T&
cmMaximum(const T& l, const T& r) {return l > r ? l : r;}
template<typename T> const T&
cmMinimum(const T& l, const T& r) {return l < r ? l : r;}
#else
#define cmMinimum std::min
#define cmMaximum std::max
#endif

//----------------------------------------------------------------------------
std::pair<bool, const char*> consistentNumberProperty(const char *lhs,
                                                      const char *rhs,
                                                      CompatibleType t)
{
  char *pEnd;

#if defined(_MSC_VER)
  static const char* const null_ptr = 0;
#else
# define null_ptr 0
#endif

  long lnum = strtol(lhs, &pEnd, 0);
  if (pEnd == lhs || *pEnd != '\0' || errno == ERANGE)
    {
    return std::pair<bool, const char*>(false, null_ptr);
    }

  long rnum = strtol(rhs, &pEnd, 0);
  if (pEnd == rhs || *pEnd != '\0' || errno == ERANGE)
    {
    return std::pair<bool, const char*>(false, null_ptr);
    }

#if !defined(_MSC_VER)
#undef null_ptr
#endif

  if (t == NumberMaxType)
    {
    return std::make_pair(true, cmMaximum(lnum, rnum) == lnum ? lhs : rhs);
    }
  else
    {
    return std::make_pair(true, cmMinimum(lnum, rnum) == lnum ? lhs : rhs);
    }
}

//----------------------------------------------------------------------------
template<>
std::pair<bool, const char*> consistentProperty(const char *lhs,
                                                const char *rhs,
                                                CompatibleType t)
{
  if (!lhs && !rhs)
    {
    return std::make_pair(true, lhs);
    }
  if (!lhs)
    {
    return std::make_pair(true, rhs);
    }
  if (!rhs)
    {
    return std::make_pair(true, lhs);
    }

#if defined(_MSC_VER)
  static const char* const null_ptr = 0;
#else
# define null_ptr 0
#endif

  switch(t)
  {
  case BoolType:
    assert(!"consistentProperty for strings called with BoolType");
    return std::pair<bool, const char*>(false, null_ptr);
  case StringType:
    return consistentStringProperty(lhs, rhs);
  case NumberMinType:
  case NumberMaxType:
    return consistentNumberProperty(lhs, rhs, t);
  }
  assert(!"Unreachable!");
  return std::pair<bool, const char*>(false, null_ptr);

#if !defined(_MSC_VER)
#undef null_ptr
#endif

}

template<typename PropertyType>
PropertyType impliedValue(PropertyType);
template<>
bool impliedValue<bool>(bool)
{
  return false;
}
template<>
const char* impliedValue<const char*>(const char*)
{
  return "";
}


template<typename PropertyType>
std::string valueAsString(PropertyType);
template<>
std::string valueAsString<bool>(bool value)
{
  return value ? "TRUE" : "FALSE";
}
template<>
std::string valueAsString<const char*>(const char* value)
{
  return value ? value : "(unset)";
}

//----------------------------------------------------------------------------
void
cmTarget::ReportPropertyOrigin(const std::string &p,
                               const std::string &result,
                               const std::string &report,
                               const std::string &compatibilityType) const
{
  std::vector<std::string> debugProperties;
  const char *debugProp =
          this->Makefile->GetDefinition("CMAKE_DEBUG_TARGET_PROPERTIES");
  if (debugProp)
    {
    cmSystemTools::ExpandListArgument(debugProp, debugProperties);
    }

  bool debugOrigin = !this->DebugCompatiblePropertiesDone[p]
                    && std::find(debugProperties.begin(),
                                 debugProperties.end(),
                                 p)
                        != debugProperties.end();

  if (this->Makefile->IsGeneratingBuildSystem())
    {
    this->DebugCompatiblePropertiesDone[p] = true;
    }
  if (!debugOrigin)
    {
    return;
    }

  std::string areport = compatibilityType;
  areport += std::string(" of property \"") + p + "\" for target \"";
  areport += std::string(this->GetName());
  areport += "\" (result: \"";
  areport += result;
  areport += "\"):\n" + report;

  this->Makefile->GetCMakeInstance()->IssueMessage(cmake::LOG, areport);
}

//----------------------------------------------------------------------------
std::string compatibilityType(CompatibleType t)
{
  switch(t)
    {
    case BoolType:
      return "Boolean compatibility";
    case StringType:
      return "String compatibility";
    case NumberMaxType:
      return "Numeric maximum compatibility";
    case NumberMinType:
      return "Numeric minimum compatibility";
    }
  assert(!"Unreachable!");
  return "";
}

//----------------------------------------------------------------------------
std::string compatibilityAgree(CompatibleType t, bool dominant)
{
  switch(t)
    {
    case BoolType:
    case StringType:
      return dominant ? "(Disagree)\n" : "(Agree)\n";
    case NumberMaxType:
    case NumberMinType:
      return dominant ? "(Dominant)\n" : "(Ignored)\n";
    }
  assert(!"Unreachable!");
  return "";
}

//----------------------------------------------------------------------------
template<typename PropertyType>
PropertyType checkInterfacePropertyCompatibility(cmTarget const* tgt,
                                          const std::string &p,
                                          const std::string& config,
                                          const char *defaultValue,
                                          CompatibleType t,
                                          PropertyType *)
{
  PropertyType propContent = getTypedProperty<PropertyType>(tgt, p.c_str(),
                                                            0);
  const bool explicitlySet = tgt->GetProperties()
                                  .find(p)
                                  != tgt->GetProperties().end();
  const bool impliedByUse =
          tgt->IsNullImpliedByLinkLibraries(p);
  assert((impliedByUse ^ explicitlySet)
      || (!impliedByUse && !explicitlySet));

  std::vector<cmTarget const*> const& deps =
    tgt->GetLinkImplementationClosure(config);

  if(deps.empty())
    {
    return propContent;
    }
  bool propInitialized = explicitlySet;

  std::string report = " * Target \"";
  report += tgt->GetName();
  if (explicitlySet)
    {
    report += "\" has property content \"";
    report += valueAsString<PropertyType>(propContent);
    report += "\"\n";
    }
  else if (impliedByUse)
    {
    report += "\" property is implied by use.\n";
    }
  else
    {
    report += "\" property not set.\n";
    }

  for(std::vector<cmTarget const*>::const_iterator li =
      deps.begin();
      li != deps.end(); ++li)
    {
    // An error should be reported if one dependency
    // has INTERFACE_POSITION_INDEPENDENT_CODE ON and the other
    // has INTERFACE_POSITION_INDEPENDENT_CODE OFF, or if the
    // target itself has a POSITION_INDEPENDENT_CODE which disagrees
    // with a dependency.

    cmTarget const* theTarget = *li;

    const bool ifaceIsSet = theTarget->GetProperties()
                            .find("INTERFACE_" + p)
                            != theTarget->GetProperties().end();
    PropertyType ifacePropContent =
                    getTypedProperty<PropertyType>(theTarget,
                              ("INTERFACE_" + p).c_str(), 0);

    std::string reportEntry;
    if (ifaceIsSet)
      {
      reportEntry += " * Target \"";
      reportEntry += theTarget->GetName();
      reportEntry += "\" property value \"";
      reportEntry += valueAsString<PropertyType>(ifacePropContent);
      reportEntry += "\" ";
      }

    if (explicitlySet)
      {
      if (ifaceIsSet)
        {
        std::pair<bool, PropertyType> consistent =
                                  consistentProperty(propContent,
                                                     ifacePropContent, t);
        report += reportEntry;
        report += compatibilityAgree(t, propContent != consistent.second);
        if (!consistent.first)
          {
          cmOStringStream e;
          e << "Property " << p << " on target \""
            << tgt->GetName() << "\" does\nnot match the "
            "INTERFACE_" << p << " property requirement\nof "
            "dependency \"" << theTarget->GetName() << "\".\n";
          cmSystemTools::Error(e.str().c_str());
          break;
          }
        else
          {
          propContent = consistent.second;
          continue;
          }
        }
      else
        {
        // Explicitly set on target and not set in iface. Can't disagree.
        continue;
        }
      }
    else if (impliedByUse)
      {
      propContent = impliedValue<PropertyType>(propContent);

      if (ifaceIsSet)
        {
        std::pair<bool, PropertyType> consistent =
                                  consistentProperty(propContent,
                                                     ifacePropContent, t);
        report += reportEntry;
        report += compatibilityAgree(t, propContent != consistent.second);
        if (!consistent.first)
          {
          cmOStringStream e;
          e << "Property " << p << " on target \""
            << tgt->GetName() << "\" is\nimplied to be " << defaultValue
            << " because it was used to determine the link libraries\n"
               "already. The INTERFACE_" << p << " property on\ndependency \""
            << theTarget->GetName() << "\" is in conflict.\n";
          cmSystemTools::Error(e.str().c_str());
          break;
          }
        else
          {
          propContent = consistent.second;
          continue;
          }
        }
      else
        {
        // Implicitly set on target and not set in iface. Can't disagree.
        continue;
        }
      }
    else
      {
      if (ifaceIsSet)
        {
        if (propInitialized)
          {
          std::pair<bool, PropertyType> consistent =
                                    consistentProperty(propContent,
                                                       ifacePropContent, t);
          report += reportEntry;
          report += compatibilityAgree(t, propContent != consistent.second);
          if (!consistent.first)
            {
            cmOStringStream e;
            e << "The INTERFACE_" << p << " property of \""
              << theTarget->GetName() << "\" does\nnot agree with the value "
                "of " << p << " already determined\nfor \""
              << tgt->GetName() << "\".\n";
            cmSystemTools::Error(e.str().c_str());
            break;
            }
          else
            {
            propContent = consistent.second;
            continue;
            }
          }
        else
          {
          report += reportEntry + "(Interface set)\n";
          propContent = ifacePropContent;
          propInitialized = true;
          }
        }
      else
        {
        // Not set. Nothing to agree on.
        continue;
        }
      }
    }

  tgt->ReportPropertyOrigin(p, valueAsString<PropertyType>(propContent),
                            report, compatibilityType(t));
  return propContent;
}

//----------------------------------------------------------------------------
bool cmTarget::GetLinkInterfaceDependentBoolProperty(const std::string &p,
                                              const std::string& config) const
{
  return checkInterfacePropertyCompatibility<bool>(this, p, config, "FALSE",
                                                   BoolType, 0);
}

//----------------------------------------------------------------------------
const char * cmTarget::GetLinkInterfaceDependentStringProperty(
                                              const std::string &p,
                                              const std::string& config) const
{
  return checkInterfacePropertyCompatibility<const char *>(this,
                                                           p,
                                                           config,
                                                           "empty",
                                                           StringType, 0);
}

//----------------------------------------------------------------------------
const char * cmTarget::GetLinkInterfaceDependentNumberMinProperty(
                                              const std::string &p,
                                              const std::string& config) const
{
  return checkInterfacePropertyCompatibility<const char *>(this,
                                                           p,
                                                           config,
                                                           "empty",
                                                           NumberMinType, 0);
}

//----------------------------------------------------------------------------
const char * cmTarget::GetLinkInterfaceDependentNumberMaxProperty(
                                              const std::string &p,
                                              const std::string& config) const
{
  return checkInterfacePropertyCompatibility<const char *>(this,
                                                           p,
                                                           config,
                                                           "empty",
                                                           NumberMaxType, 0);
}

//----------------------------------------------------------------------------
bool isLinkDependentProperty(cmTarget const* tgt, const std::string &p,
                             const std::string& interfaceProperty,
                             const std::string& config)
{
  std::vector<cmTarget const*> const& deps =
    tgt->GetLinkImplementationClosure(config);

  if(deps.empty())
    {
    return false;
    }

  for(std::vector<cmTarget const*>::const_iterator li = deps.begin();
      li != deps.end(); ++li)
    {
    const char *prop = (*li)->GetProperty(interfaceProperty);
    if (!prop)
      {
      continue;
      }

    std::vector<std::string> props;
    cmSystemTools::ExpandListArgument(prop, props);

    for(std::vector<std::string>::iterator pi = props.begin();
        pi != props.end(); ++pi)
      {
      if (*pi == p)
        {
        return true;
        }
      }
    }

  return false;
}

//----------------------------------------------------------------------------
bool cmTarget::IsLinkInterfaceDependentBoolProperty(const std::string &p,
                                           const std::string& config) const
{
  if (this->TargetTypeValue == OBJECT_LIBRARY
      || this->TargetTypeValue == INTERFACE_LIBRARY)
    {
    return false;
    }
  return (p == "POSITION_INDEPENDENT_CODE") ||
    isLinkDependentProperty(this, p, "COMPATIBLE_INTERFACE_BOOL",
                                 config);
}

//----------------------------------------------------------------------------
bool cmTarget::IsLinkInterfaceDependentStringProperty(const std::string &p,
                                    const std::string& config) const
{
  if (this->TargetTypeValue == OBJECT_LIBRARY
      || this->TargetTypeValue == INTERFACE_LIBRARY)
    {
    return false;
    }
  return (p == "AUTOUIC_OPTIONS") ||
    isLinkDependentProperty(this, p, "COMPATIBLE_INTERFACE_STRING",
                                 config);
}

//----------------------------------------------------------------------------
bool cmTarget::IsLinkInterfaceDependentNumberMinProperty(const std::string &p,
                                    const std::string& config) const
{
  if (this->TargetTypeValue == OBJECT_LIBRARY
      || this->TargetTypeValue == INTERFACE_LIBRARY)
    {
    return false;
    }
  return isLinkDependentProperty(this, p, "COMPATIBLE_INTERFACE_NUMBER_MIN",
                                 config);
}

//----------------------------------------------------------------------------
bool cmTarget::IsLinkInterfaceDependentNumberMaxProperty(const std::string &p,
                                    const std::string& config) const
{
  if (this->TargetTypeValue == OBJECT_LIBRARY
      || this->TargetTypeValue == INTERFACE_LIBRARY)
    {
    return false;
    }
  return isLinkDependentProperty(this, p, "COMPATIBLE_INTERFACE_NUMBER_MAX",
                                 config);
}

//----------------------------------------------------------------------------
void
cmTarget::GetObjectLibrariesCMP0026(std::vector<cmTarget*>& objlibs) const
{
  // At configure-time, this method can be called as part of getting the
  // LOCATION property or to export() a file to be include()d.  However
  // there is no cmGeneratorTarget at configure-time, so search the SOURCES
  // for TARGET_OBJECTS instead for backwards compatibility with OLD
  // behavior of CMP0024 and CMP0026 only.
  typedef cmTargetInternals::TargetPropertyEntry
                              TargetPropertyEntry;
  for(std::vector<TargetPropertyEntry*>::const_iterator
        i = this->Internal->SourceEntries.begin();
      i != this->Internal->SourceEntries.end(); ++i)
    {
    std::string entry = (*i)->ge->GetInput();

    std::vector<std::string> files;
    cmSystemTools::ExpandListArgument(entry, files);
    for (std::vector<std::string>::const_iterator
        li = files.begin(); li != files.end(); ++li)
      {
      if(cmHasLiteralPrefix(*li, "$<TARGET_OBJECTS:") &&
          (*li)[li->size() - 1] == '>')
        {
        std::string objLibName = li->substr(17, li->size()-18);

        if (cmGeneratorExpression::Find(objLibName) != std::string::npos)
          {
          continue;
          }
        cmTarget *objLib = this->Makefile->FindTargetToUse(objLibName.c_str());
        if(objLib)
          {
          objlibs.push_back(objLib);
          }
        }
      }
    }
}

//----------------------------------------------------------------------------
void cmTarget::GetLanguages(std::set<std::string>& languages,
                            const std::string& config,
                            cmTarget const* head) const
{
  std::vector<cmSourceFile*> sourceFiles;
  this->GetSourceFiles(sourceFiles, config, head);
  for(std::vector<cmSourceFile*>::const_iterator
        i = sourceFiles.begin(); i != sourceFiles.end(); ++i)
    {
    const std::string& lang = (*i)->GetLanguage();
    if(!lang.empty())
      {
      languages.insert(lang);
      }
    }

  std::vector<cmTarget*> objectLibraries;
  std::vector<cmSourceFile const*> externalObjects;
  if (this->Makefile->GetGeneratorTargets().empty())
    {
    this->GetObjectLibrariesCMP0026(objectLibraries);
    }
  else
    {
    cmGeneratorTarget* gt = this->Makefile->GetLocalGenerator()
                                ->GetGlobalGenerator()
                                ->GetGeneratorTarget(this);
    gt->GetExternalObjects(externalObjects, config);
    for(std::vector<cmSourceFile const*>::const_iterator
          i = externalObjects.begin(); i != externalObjects.end(); ++i)
      {
      std::string objLib = (*i)->GetObjectLibrary();
      if (cmTarget* tgt = this->Makefile->FindTargetToUse(objLib))
        {
        objectLibraries.push_back(tgt);
        }
      }
    }
  for(std::vector<cmTarget*>::const_iterator
      i = objectLibraries.begin(); i != objectLibraries.end(); ++i)
    {
    (*i)->GetLanguages(languages, config, head);
    }
}

//----------------------------------------------------------------------------
bool cmTarget::IsChrpathUsed(const std::string& config) const
{
  // Only certain target types have an rpath.
  if(!(this->GetType() == cmTarget::SHARED_LIBRARY ||
       this->GetType() == cmTarget::MODULE_LIBRARY ||
       this->GetType() == cmTarget::EXECUTABLE))
    {
    return false;
    }

  // If the target will not be installed we do not need to change its
  // rpath.
  if(!this->GetHaveInstallRule())
    {
    return false;
    }

  // Skip chrpath if skipping rpath altogether.
  if(this->Makefile->IsOn("CMAKE_SKIP_RPATH"))
    {
    return false;
    }

  // Skip chrpath if it does not need to be changed at install time.
  if(this->GetPropertyAsBool("BUILD_WITH_INSTALL_RPATH"))
    {
    return false;
    }

  // Allow the user to disable builtin chrpath explicitly.
  if(this->Makefile->IsOn("CMAKE_NO_BUILTIN_CHRPATH"))
    {
    return false;
    }

  if(this->Makefile->IsOn("CMAKE_PLATFORM_HAS_INSTALLNAME"))
    {
    return true;
    }

#if defined(CMAKE_USE_ELF_PARSER)
  // Enable if the rpath flag uses a separator and the target uses ELF
  // binaries.
  std::string ll = this->GetLinkerLanguage(config);
  if(!ll.empty())
    {
    std::string sepVar = "CMAKE_SHARED_LIBRARY_RUNTIME_";
    sepVar += ll;
    sepVar += "_FLAG_SEP";
    const char* sep = this->Makefile->GetDefinition(sepVar);
    if(sep && *sep)
      {
      // TODO: Add ELF check to ABI detection and get rid of
      // CMAKE_EXECUTABLE_FORMAT.
      if(const char* fmt =
         this->Makefile->GetDefinition("CMAKE_EXECUTABLE_FORMAT"))
        {
        return strcmp(fmt, "ELF") == 0;
        }
      }
    }
#endif
  static_cast<void>(config);
  return false;
}

//----------------------------------------------------------------------------
cmTarget::ImportInfo const*
cmTarget::GetImportInfo(const std::string& config) const
{
  // There is no imported information for non-imported targets.
  if(!this->IsImported())
    {
    return 0;
    }

  // Lookup/compute/cache the import information for this
  // configuration.
  std::string config_upper;
  if(!config.empty())
    {
    config_upper = cmSystemTools::UpperCase(config);
    }
  else
    {
    config_upper = "NOCONFIG";
    }
  typedef cmTargetInternals::ImportInfoMapType ImportInfoMapType;

  ImportInfoMapType::const_iterator i =
    this->Internal->ImportInfoMap.find(config_upper);
  if(i == this->Internal->ImportInfoMap.end())
    {
    ImportInfo info;
    this->ComputeImportInfo(config_upper, info);
    ImportInfoMapType::value_type entry(config_upper, info);
    i = this->Internal->ImportInfoMap.insert(entry).first;
    }

  if(this->GetType() == INTERFACE_LIBRARY)
    {
    return &i->second;
    }
  // If the location is empty then the target is not available for
  // this configuration.
  if(i->second.Location.empty() && i->second.ImportLibrary.empty())
    {
    return 0;
    }

  // Return the import information.
  return &i->second;
}

bool cmTarget::GetMappedConfig(std::string const& desired_config,
                               const char** loc,
                               const char** imp,
                               std::string& suffix) const
{
  if (this->GetType() == INTERFACE_LIBRARY)
    {
    // This method attempts to find a config-specific LOCATION for the
    // IMPORTED library. In the case of INTERFACE_LIBRARY, there is no
    // LOCATION at all, so leaving *loc and *imp unchanged is the appropriate
    // and valid response.
    return true;
    }

  // Track the configuration-specific property suffix.
  suffix = "_";
  suffix += desired_config;

  std::vector<std::string> mappedConfigs;
  {
  std::string mapProp = "MAP_IMPORTED_CONFIG_";
  mapProp += desired_config;
  if(const char* mapValue = this->GetProperty(mapProp))
    {
    cmSystemTools::ExpandListArgument(mapValue, mappedConfigs);
    }
  }

  // If we needed to find one of the mapped configurations but did not
  // On a DLL platform there may be only IMPORTED_IMPLIB for a shared
  // library or an executable with exports.
  bool allowImp = this->HasImportLibrary();

  // If a mapping was found, check its configurations.
  for(std::vector<std::string>::const_iterator mci = mappedConfigs.begin();
      !*loc && !*imp && mci != mappedConfigs.end(); ++mci)
    {
    // Look for this configuration.
    std::string mcUpper = cmSystemTools::UpperCase(*mci);
    std::string locProp = "IMPORTED_LOCATION_";
    locProp += mcUpper;
    *loc = this->GetProperty(locProp);
    if(allowImp)
      {
      std::string impProp = "IMPORTED_IMPLIB_";
      impProp += mcUpper;
      *imp = this->GetProperty(impProp);
      }

    // If it was found, use it for all properties below.
    if(*loc || *imp)
      {
      suffix = "_";
      suffix += mcUpper;
      }
    }

  // If we needed to find one of the mapped configurations but did not
  // then the target is not found.  The project does not want any
  // other configuration.
  if(!mappedConfigs.empty() && !*loc && !*imp)
    {
    return false;
    }

  // If we have not yet found it then there are no mapped
  // configurations.  Look for an exact-match.
  if(!*loc && !*imp)
    {
    std::string locProp = "IMPORTED_LOCATION";
    locProp += suffix;
    *loc = this->GetProperty(locProp);
    if(allowImp)
      {
      std::string impProp = "IMPORTED_IMPLIB";
      impProp += suffix;
      *imp = this->GetProperty(impProp);
      }
    }

  // If we have not yet found it then there are no mapped
  // configurations and no exact match.
  if(!*loc && !*imp)
    {
    // The suffix computed above is not useful.
    suffix = "";

    // Look for a configuration-less location.  This may be set by
    // manually-written code.
    *loc = this->GetProperty("IMPORTED_LOCATION");
    if(allowImp)
      {
      *imp = this->GetProperty("IMPORTED_IMPLIB");
      }
    }

  // If we have not yet found it then the project is willing to try
  // any available configuration.
  if(!*loc && !*imp)
    {
    std::vector<std::string> availableConfigs;
    if(const char* iconfigs = this->GetProperty("IMPORTED_CONFIGURATIONS"))
      {
      cmSystemTools::ExpandListArgument(iconfigs, availableConfigs);
      }
    for(std::vector<std::string>::const_iterator
          aci = availableConfigs.begin();
        !*loc && !*imp && aci != availableConfigs.end(); ++aci)
      {
      suffix = "_";
      suffix += cmSystemTools::UpperCase(*aci);
      std::string locProp = "IMPORTED_LOCATION";
      locProp += suffix;
      *loc = this->GetProperty(locProp);
      if(allowImp)
        {
        std::string impProp = "IMPORTED_IMPLIB";
        impProp += suffix;
        *imp = this->GetProperty(impProp);
        }
      }
    }
  // If we have not yet found it then the target is not available.
  if(!*loc && !*imp)
    {
    return false;
    }

  return true;
}

//----------------------------------------------------------------------------
void cmTarget::ComputeImportInfo(std::string const& desired_config,
                                 ImportInfo& info) const
{
  // This method finds information about an imported target from its
  // properties.  The "IMPORTED_" namespace is reserved for properties
  // defined by the project exporting the target.

  // Initialize members.
  info.NoSOName = false;

  const char* loc = 0;
  const char* imp = 0;
  std::string suffix;
  if (!this->GetMappedConfig(desired_config, &loc, &imp, suffix))
    {
    return;
    }

  // Get the link interface.
  {
  std::string linkProp = "INTERFACE_LINK_LIBRARIES";
  const char *propertyLibs = this->GetProperty(linkProp);

  if (this->GetType() != INTERFACE_LIBRARY)
    {
    if(!propertyLibs)
      {
      linkProp = "IMPORTED_LINK_INTERFACE_LIBRARIES";
      linkProp += suffix;
      propertyLibs = this->GetProperty(linkProp);
      }

    if(!propertyLibs)
      {
      linkProp = "IMPORTED_LINK_INTERFACE_LIBRARIES";
      propertyLibs = this->GetProperty(linkProp);
      }
    }
  if(propertyLibs)
    {
    info.LibrariesProp = linkProp;
    info.Libraries = propertyLibs;
    }
  }
  if(this->GetType() == INTERFACE_LIBRARY)
    {
    return;
    }

  // A provided configuration has been chosen.  Load the
  // configuration's properties.

  // Get the location.
  if(loc)
    {
    info.Location = loc;
    }
  else
    {
    std::string impProp = "IMPORTED_LOCATION";
    impProp += suffix;
    if(const char* config_location = this->GetProperty(impProp))
      {
      info.Location = config_location;
      }
    else if(const char* location = this->GetProperty("IMPORTED_LOCATION"))
      {
      info.Location = location;
      }
    }

  // Get the soname.
  if(this->GetType() == cmTarget::SHARED_LIBRARY)
    {
    std::string soProp = "IMPORTED_SONAME";
    soProp += suffix;
    if(const char* config_soname = this->GetProperty(soProp))
      {
      info.SOName = config_soname;
      }
    else if(const char* soname = this->GetProperty("IMPORTED_SONAME"))
      {
      info.SOName = soname;
      }
    }

  // Get the "no-soname" mark.
  if(this->GetType() == cmTarget::SHARED_LIBRARY)
    {
    std::string soProp = "IMPORTED_NO_SONAME";
    soProp += suffix;
    if(const char* config_no_soname = this->GetProperty(soProp))
      {
      info.NoSOName = cmSystemTools::IsOn(config_no_soname);
      }
    else if(const char* no_soname = this->GetProperty("IMPORTED_NO_SONAME"))
      {
      info.NoSOName = cmSystemTools::IsOn(no_soname);
      }
    }

  // Get the import library.
  if(imp)
    {
    info.ImportLibrary = imp;
    }
  else if(this->GetType() == cmTarget::SHARED_LIBRARY ||
          this->IsExecutableWithExports())
    {
    std::string impProp = "IMPORTED_IMPLIB";
    impProp += suffix;
    if(const char* config_implib = this->GetProperty(impProp))
      {
      info.ImportLibrary = config_implib;
      }
    else if(const char* implib = this->GetProperty("IMPORTED_IMPLIB"))
      {
      info.ImportLibrary = implib;
      }
    }

  // Get the link dependencies.
  {
  std::string linkProp = "IMPORTED_LINK_DEPENDENT_LIBRARIES";
  linkProp += suffix;
  if(const char* config_libs = this->GetProperty(linkProp))
    {
    info.SharedDeps = config_libs;
    }
  else if(const char* libs =
          this->GetProperty("IMPORTED_LINK_DEPENDENT_LIBRARIES"))
    {
    info.SharedDeps = libs;
    }
  }

  // Get the link languages.
  if(this->LinkLanguagePropagatesToDependents())
    {
    std::string linkProp = "IMPORTED_LINK_INTERFACE_LANGUAGES";
    linkProp += suffix;
    if(const char* config_libs = this->GetProperty(linkProp))
      {
      info.Languages = config_libs;
      }
    else if(const char* libs =
            this->GetProperty("IMPORTED_LINK_INTERFACE_LANGUAGES"))
      {
      info.Languages = libs;
      }
    }

  // Get the cyclic repetition count.
  if(this->GetType() == cmTarget::STATIC_LIBRARY)
    {
    std::string linkProp = "IMPORTED_LINK_INTERFACE_MULTIPLICITY";
    linkProp += suffix;
    if(const char* config_reps = this->GetProperty(linkProp))
      {
      sscanf(config_reps, "%u", &info.Multiplicity);
      }
    else if(const char* reps =
            this->GetProperty("IMPORTED_LINK_INTERFACE_MULTIPLICITY"))
      {
      sscanf(reps, "%u", &info.Multiplicity);
      }
    }
}

//----------------------------------------------------------------------------
cmTarget::LinkInterface const* cmTarget::GetLinkInterface(
                                                  const std::string& config,
                                                  cmTarget const* head) const
{
  // Imported targets have their own link interface.
  if(this->IsImported())
    {
    return this->GetImportLinkInterface(config, head, false);
    }

  // Link interfaces are not supported for executables that do not
  // export symbols.
  if(this->GetType() == cmTarget::EXECUTABLE &&
     !this->IsExecutableWithExports())
    {
    return 0;
    }

  // Lookup any existing link interface for this configuration.
  TargetConfigPair key(head, cmSystemTools::UpperCase(config));

  cmTargetInternals::LinkInterfaceMapType::iterator
    i = this->Internal->LinkInterfaceMap.find(key);
  if(i == this->Internal->LinkInterfaceMap.end())
    {
    // Compute the link interface for this configuration.
    cmTargetInternals::OptionalLinkInterface iface;
    iface.ExplicitLibraries =
      this->ComputeLinkInterfaceLibraries(config, iface, head, false,
                                          iface.Exists);
    if (iface.Exists)
      {
      this->Internal->ComputeLinkInterface(this, config, iface,
                                           head, iface.ExplicitLibraries);
      }

    // Store the information for this configuration.
    cmTargetInternals::LinkInterfaceMapType::value_type entry(key, iface);
    i = this->Internal->LinkInterfaceMap.insert(entry).first;
    }
  else if(!i->second.Complete && i->second.Exists)
    {
    this->Internal->ComputeLinkInterface(this, config, i->second, head,
                                         i->second.ExplicitLibraries);
    }

  return i->second.Exists ? &i->second : 0;
}

//----------------------------------------------------------------------------
cmTarget::LinkInterface const*
cmTarget::GetLinkInterfaceLibraries(const std::string& config,
                                    cmTarget const* head,
                                    bool usage_requirements_only) const
{
  // Imported targets have their own link interface.
  if(this->IsImported())
    {
    return this->GetImportLinkInterface(config, head, usage_requirements_only);
    }

  // Link interfaces are not supported for executables that do not
  // export symbols.
  if(this->GetType() == cmTarget::EXECUTABLE &&
     !this->IsExecutableWithExports())
    {
    return 0;
    }

  // Lookup any existing link interface for this configuration.
  TargetConfigPair key(head, cmSystemTools::UpperCase(config));
  cmTargetInternals::LinkInterfaceMapType& lim =
    (usage_requirements_only ?
     this->Internal->LinkInterfaceUsageRequirementsOnlyMap :
     this->Internal->LinkInterfaceMap);

  cmTargetInternals::LinkInterfaceMapType::iterator i = lim.find(key);
  if(i == lim.end())
    {
    // Compute the link interface for this configuration.
    cmTargetInternals::OptionalLinkInterface iface;
    iface.ExplicitLibraries =
      this->ComputeLinkInterfaceLibraries(config, iface, head,
                                          usage_requirements_only,
                                          iface.Exists);

    // Store the information for this configuration.
    cmTargetInternals::LinkInterfaceMapType::value_type entry(key, iface);
    i = lim.insert(entry).first;
    }

  return i->second.Exists ? &i->second : 0;
}

//----------------------------------------------------------------------------
cmTarget::LinkInterface const*
cmTarget::GetImportLinkInterface(const std::string& config,
                                 cmTarget const* headTarget,
                                 bool usage_requirements_only) const
{
  cmTarget::ImportInfo const* info = this->GetImportInfo(config);
  if(!info)
    {
    return 0;
    }

  TargetConfigPair key(headTarget, cmSystemTools::UpperCase(config));
  cmTargetInternals::ImportLinkInterfaceMapType& lim =
    (usage_requirements_only ?
     this->Internal->ImportLinkInterfaceUsageRequirementsOnlyMap :
     this->Internal->ImportLinkInterfaceMap);

  cmTargetInternals::ImportLinkInterfaceMapType::iterator i = lim.find(key);
  if(i == lim.end())
    {
    LinkInterface iface;
    iface.Multiplicity = info->Multiplicity;
    cmSystemTools::ExpandListArgument(info->Languages, iface.Languages);
    this->ExpandLinkItems(info->LibrariesProp, info->Libraries, config,
                          headTarget, usage_requirements_only,
                          iface.Libraries);
    {
    std::vector<std::string> deps;
    cmSystemTools::ExpandListArgument(info->SharedDeps, deps);
    this->LookupLinkItems(deps, iface.SharedDeps);
    }

    cmTargetInternals::ImportLinkInterfaceMapType::value_type
      entry(key, iface);
    i = lim.insert(entry).first;
    }
  return &i->second;
}

//----------------------------------------------------------------------------
void processILibs(const std::string& config,
                  cmTarget const* headTarget,
                  cmLinkItem const& item,
                  std::vector<cmTarget const*>& tgts,
                  std::set<cmTarget const*>& emitted)
{
  if (item.Target && emitted.insert(item.Target).second)
    {
    tgts.push_back(item.Target);
    if(cmTarget::LinkInterface const* iface =
       item.Target->GetLinkInterfaceLibraries(config, headTarget, false))
      {
      for(std::vector<cmLinkItem>::const_iterator
            it = iface->Libraries.begin();
          it != iface->Libraries.end(); ++it)
        {
        processILibs(config, headTarget, *it, tgts, emitted);
        }
      }
    }
}

//----------------------------------------------------------------------------
std::vector<cmTarget const*> const&
cmTarget::GetLinkImplementationClosure(const std::string& config) const
{
  std::vector<cmTarget const*>& tgts =
    this->Internal->CachedLinkImplementationClosure[config];
  if(!this->Internal->CacheLinkImplementationClosureDone[config])
    {
    this->Internal->CacheLinkImplementationClosureDone[config] = true;
    std::set<cmTarget const*> emitted;

    cmTarget::LinkImplementation const* impl
      = this->GetLinkImplementationLibraries(config);

    for(std::vector<cmLinkItem>::const_iterator it = impl->Libraries.begin();
        it != impl->Libraries.end(); ++it)
      {
      processILibs(config, this, *it, tgts , emitted);
      }
    }
  return tgts;
}

//----------------------------------------------------------------------------
void cmTarget::GetTransitivePropertyTargets(const std::string& config,
                                      cmTarget const* headTarget,
                                      std::vector<cmTarget const*> &tgts) const
{
  // The $<LINK_ONLY> expression may be in a link interface to specify private
  // link dependencies that are otherwise excluded from usage requirements.
  // Currently $<LINK_ONLY> is internal to CMake and only ever added by
  // target_link_libraries for PRIVATE dependencies of STATIC libraries in
  // INTERFACE_LINK_LIBRARIES which is used under CMP0022 NEW behavior.
  bool usage_requirements_only =
    this->GetType() == STATIC_LIBRARY &&
    this->GetPolicyStatusCMP0022() != cmPolicies::WARN &&
    this->GetPolicyStatusCMP0022() != cmPolicies::OLD;
  if(cmTarget::LinkInterface const* iface =
     this->GetLinkInterfaceLibraries(config, headTarget,
                                     usage_requirements_only))
    {
    for(std::vector<cmLinkItem>::const_iterator it = iface->Libraries.begin();
        it != iface->Libraries.end(); ++it)
      {
      if (it->Target)
        {
        tgts.push_back(it->Target);
        }
      }
    }
}

//----------------------------------------------------------------------------
const char* cmTarget::ComputeLinkInterfaceLibraries(const std::string& config,
                                           LinkInterface& iface,
                                           cmTarget const* headTarget,
                                           bool usage_requirements_only,
                                           bool &exists) const
{
  // Construct the property name suffix for this configuration.
  std::string suffix = "_";
  if(!config.empty())
    {
    suffix += cmSystemTools::UpperCase(config);
    }
  else
    {
    suffix += "NOCONFIG";
    }

  // An explicit list of interface libraries may be set for shared
  // libraries and executables that export symbols.
  const char* explicitLibraries = 0;
  std::string linkIfaceProp;
  if(this->PolicyStatusCMP0022 != cmPolicies::OLD &&
     this->PolicyStatusCMP0022 != cmPolicies::WARN)
    {
    // CMP0022 NEW behavior is to use INTERFACE_LINK_LIBRARIES.
    linkIfaceProp = "INTERFACE_LINK_LIBRARIES";
    explicitLibraries = this->GetProperty(linkIfaceProp);
    }
  else if(this->GetType() == cmTarget::SHARED_LIBRARY ||
          this->IsExecutableWithExports())
    {
    // CMP0022 OLD behavior is to use LINK_INTERFACE_LIBRARIES if set on a
    // shared lib or executable.

    // Lookup the per-configuration property.
    linkIfaceProp = "LINK_INTERFACE_LIBRARIES";
    linkIfaceProp += suffix;
    explicitLibraries = this->GetProperty(linkIfaceProp);

    // If not set, try the generic property.
    if(!explicitLibraries)
      {
      linkIfaceProp = "LINK_INTERFACE_LIBRARIES";
      explicitLibraries = this->GetProperty(linkIfaceProp);
      }
    }

  if(explicitLibraries && this->PolicyStatusCMP0022 == cmPolicies::WARN &&
     !this->Internal->PolicyWarnedCMP0022)
    {
    // Compare the explicitly set old link interface properties to the
    // preferred new link interface property one and warn if different.
    const char* newExplicitLibraries =
      this->GetProperty("INTERFACE_LINK_LIBRARIES");
    if (newExplicitLibraries
        && strcmp(newExplicitLibraries, explicitLibraries) != 0)
      {
      cmOStringStream w;
      w <<
        (this->Makefile->GetPolicies()
         ->GetPolicyWarning(cmPolicies::CMP0022)) << "\n"
        "Target \"" << this->GetName() << "\" has an "
        "INTERFACE_LINK_LIBRARIES property which differs from its " <<
        linkIfaceProp << " properties."
        "\n"
        "INTERFACE_LINK_LIBRARIES:\n"
        "  " << newExplicitLibraries << "\n" <<
        linkIfaceProp << ":\n"
        "  " << (explicitLibraries ? explicitLibraries : "(empty)") << "\n";
      this->Makefile->IssueMessage(cmake::AUTHOR_WARNING, w.str());
      this->Internal->PolicyWarnedCMP0022 = true;
      }
    }

  // There is no implicit link interface for executables or modules
  // so if none was explicitly set then there is no link interface.
  if(!explicitLibraries &&
     (this->GetType() == cmTarget::EXECUTABLE ||
      (this->GetType() == cmTarget::MODULE_LIBRARY)))
    {
    exists = false;
    return 0;
    }
  exists = true;

  if(explicitLibraries)
    {
    // The interface libraries have been explicitly set.
    this->ExpandLinkItems(linkIfaceProp, explicitLibraries, config,
                          headTarget, usage_requirements_only,
                          iface.Libraries);
    }
  else if (this->PolicyStatusCMP0022 == cmPolicies::WARN
        || this->PolicyStatusCMP0022 == cmPolicies::OLD)
    // If CMP0022 is NEW then the plain tll signature sets the
    // INTERFACE_LINK_LIBRARIES, so if we get here then the project
    // cleared the property explicitly and we should not fall back
    // to the link implementation.
    {
    // The link implementation is the default link interface.
    LinkImplementation const* impl =
        this->GetLinkImplementationLibrariesInternal(config, headTarget);
    iface.Libraries = impl->Libraries;
    if(this->PolicyStatusCMP0022 == cmPolicies::WARN &&
       !this->Internal->PolicyWarnedCMP0022 && !usage_requirements_only)
      {
      // Compare the link implementation fallback link interface to the
      // preferred new link interface property and warn if different.
      std::vector<cmLinkItem> ifaceLibs;
      std::string newProp = "INTERFACE_LINK_LIBRARIES";
      if(const char* newExplicitLibraries = this->GetProperty(newProp))
        {
        this->ExpandLinkItems(newProp, newExplicitLibraries, config,
                              headTarget, usage_requirements_only,
                              ifaceLibs);
        }
      if (ifaceLibs != impl->Libraries)
        {
        std::string oldLibraries;
        std::string newLibraries;
        const char *sep = "";
        for(std::vector<cmLinkItem>::const_iterator it
              = impl->Libraries.begin(); it != impl->Libraries.end(); ++it)
          {
          oldLibraries += sep;
          oldLibraries += *it;
          sep = ";";
          }
        sep = "";
        for(std::vector<cmLinkItem>::const_iterator it
              = ifaceLibs.begin(); it != ifaceLibs.end(); ++it)
          {
          newLibraries += sep;
          newLibraries += *it;
          sep = ";";
          }
        if(oldLibraries.empty())
          { oldLibraries = "(empty)"; }
        if(newLibraries.empty())
          { newLibraries = "(empty)"; }

        cmOStringStream w;
        w <<
          (this->Makefile->GetPolicies()
           ->GetPolicyWarning(cmPolicies::CMP0022)) << "\n"
          "Target \"" << this->GetName() << "\" has an "
          "INTERFACE_LINK_LIBRARIES property.  "
          "This should be preferred as the source of the link interface "
          "for this library but because CMP0022 is not set CMake is "
          "ignoring the property and using the link implementation "
          "as the link interface instead."
          "\n"
          "INTERFACE_LINK_LIBRARIES:\n"
          "  " << newLibraries << "\n"
          "Link implementation:\n"
          "  " << oldLibraries << "\n";
        this->Makefile->IssueMessage(cmake::AUTHOR_WARNING, w.str());
        this->Internal->PolicyWarnedCMP0022 = true;
        }
      }
    }
  return explicitLibraries;
}

//----------------------------------------------------------------------------
void cmTargetInternals::ComputeLinkInterface(cmTarget const* thisTarget,
                                             const std::string& config,
                                             OptionalLinkInterface& iface,
                                             cmTarget const* headTarget,
                                          const char* explicitLibraries) const
{
  if(explicitLibraries)
    {
    if(thisTarget->GetType() == cmTarget::SHARED_LIBRARY
        || thisTarget->GetType() == cmTarget::STATIC_LIBRARY
        || thisTarget->GetType() == cmTarget::INTERFACE_LIBRARY)
      {
      // Shared libraries may have runtime implementation dependencies
      // on other shared libraries that are not in the interface.
      std::set<std::string> emitted;
      for(std::vector<cmLinkItem>::const_iterator
          li = iface.Libraries.begin(); li != iface.Libraries.end(); ++li)
        {
        emitted.insert(*li);
        }
      if (thisTarget->GetType() != cmTarget::INTERFACE_LIBRARY)
        {
        cmTarget::LinkImplementation const* impl =
            thisTarget->GetLinkImplementation(config);
        for(std::vector<cmLinkItem>::const_iterator
              li = impl->Libraries.begin(); li != impl->Libraries.end(); ++li)
          {
          if(emitted.insert(*li).second)
            {
            if(li->Target)
              {
              // This is a runtime dependency on another shared library.
              if(li->Target->GetType() == cmTarget::SHARED_LIBRARY)
                {
                iface.SharedDeps.push_back(*li);
                }
              }
            else
              {
              // TODO: Recognize shared library file names.  Perhaps this
              // should be moved to cmComputeLinkInformation, but that creates
              // a chicken-and-egg problem since this list is needed for its
              // construction.
              }
            }
          }
        }
      }
    }
  else if (thisTarget->PolicyStatusCMP0022 == cmPolicies::WARN
        || thisTarget->PolicyStatusCMP0022 == cmPolicies::OLD)
    {
    // The link implementation is the default link interface.
    cmTarget::LinkImplementation const*
      impl = thisTarget->GetLinkImplementationLibrariesInternal(config,
                                                                headTarget);
    iface.ImplementationIsInterface = true;
    iface.WrongConfigLibraries = impl->WrongConfigLibraries;
    }

  if(thisTarget->LinkLanguagePropagatesToDependents())
    {
    // Targets using this archive need its language runtime libraries.
    if(cmTarget::LinkImplementation const* impl =
       thisTarget->GetLinkImplementation(config))
      {
      iface.Languages = impl->Languages;
      }
    }

  if(thisTarget->GetType() == cmTarget::STATIC_LIBRARY)
    {
    // Construct the property name suffix for this configuration.
    std::string suffix = "_";
    if(!config.empty())
      {
      suffix += cmSystemTools::UpperCase(config);
      }
    else
      {
      suffix += "NOCONFIG";
      }

    // How many repetitions are needed if this library has cyclic
    // dependencies?
    std::string propName = "LINK_INTERFACE_MULTIPLICITY";
    propName += suffix;
    if(const char* config_reps = thisTarget->GetProperty(propName))
      {
      sscanf(config_reps, "%u", &iface.Multiplicity);
      }
    else if(const char* reps =
            thisTarget->GetProperty("LINK_INTERFACE_MULTIPLICITY"))
      {
      sscanf(reps, "%u", &iface.Multiplicity);
      }
    }
  iface.Complete = true;
}

//----------------------------------------------------------------------------
cmTarget::LinkImplementation const*
cmTarget::GetLinkImplementation(const std::string& config) const
{
  // There is no link implementation for imported targets.
  if(this->IsImported())
    {
    return 0;
    }

  // Populate the link implementation for this configuration.
  TargetConfigPair key(this, cmSystemTools::UpperCase(config));
  cmTargetInternals::OptionalLinkImplementation&
    impl = this->Internal->LinkImplMap[key];
  if(!impl.LibrariesDone)
    {
    impl.LibrariesDone = true;
    this->ComputeLinkImplementation(config, impl, this);
    }
  if(!impl.LanguagesDone)
    {
    impl.LanguagesDone = true;
    this->ComputeLinkImplementationLanguages(config, impl, this);
    }
  return &impl;
}

//----------------------------------------------------------------------------
cmTarget::LinkImplementation const*
cmTarget::GetLinkImplementationLibraries(const std::string& config) const
{
  return this->GetLinkImplementationLibrariesInternal(config, this);
}

//----------------------------------------------------------------------------
cmTarget::LinkImplementation const*
cmTarget::GetLinkImplementationLibrariesInternal(const std::string& config,
                                                 cmTarget const* head) const
{
  // There is no link implementation for imported targets.
  if(this->IsImported())
    {
    return 0;
    }

  // Populate the link implementation libraries for this configuration.
  TargetConfigPair key(head, cmSystemTools::UpperCase(config));
  cmTargetInternals::OptionalLinkImplementation&
    impl = this->Internal->LinkImplMap[key];
  if(!impl.LibrariesDone)
    {
    impl.LibrariesDone = true;
    this->ComputeLinkImplementation(config, impl, this);
    }
  return &impl;
}

//----------------------------------------------------------------------------
void cmTarget::ComputeLinkImplementation(const std::string& config,
                                         LinkImplementation& impl,
                                         cmTarget const* head) const
{
  // Collect libraries directly linked in this configuration.
  for (std::vector<cmValueWithOrigin>::const_iterator
      le = this->Internal->LinkImplementationPropertyEntries.begin(),
      end = this->Internal->LinkImplementationPropertyEntries.end();
      le != end; ++le)
    {
    std::vector<std::string> llibs;
    cmGeneratorExpressionDAGChecker dagChecker(
                                        this->GetName(),
                                        "LINK_LIBRARIES", 0, 0);
    cmGeneratorExpression ge(&le->Backtrace);
    cmsys::auto_ptr<cmCompiledGeneratorExpression> const cge =
      ge.Parse(le->Value);
    cmSystemTools::ExpandListArgument(cge->Evaluate(this->Makefile,
                                        config,
                                        false,
                                        head,
                                        &dagChecker),
                                      llibs);

    for(std::vector<std::string>::const_iterator li = llibs.begin();
        li != llibs.end(); ++li)
      {
      // Skip entries that resolve to the target itself or are empty.
      std::string name = this->CheckCMP0004(*li);
      if(name == this->GetName() || name.empty())
        {
        if(name == this->GetName())
          {
          bool noMessage = false;
          cmake::MessageType messageType = cmake::FATAL_ERROR;
          cmOStringStream e;
          switch(this->GetPolicyStatusCMP0038())
            {
            case cmPolicies::WARN:
              {
              e << (this->Makefile->GetPolicies()
                    ->GetPolicyWarning(cmPolicies::CMP0038)) << "\n";
              messageType = cmake::AUTHOR_WARNING;
              }
              break;
            case cmPolicies::OLD:
              noMessage = true;
            case cmPolicies::REQUIRED_IF_USED:
            case cmPolicies::REQUIRED_ALWAYS:
            case cmPolicies::NEW:
              // Issue the fatal message.
              break;
            }

          if(!noMessage)
            {
            e << "Target \"" << this->GetName() << "\" links to itself.";
            this->Makefile->GetCMakeInstance()->IssueMessage(
              messageType, e.str(), this->GetBacktrace());
            if (messageType == cmake::FATAL_ERROR)
              {
              return;
              }
            }
          }
        continue;
        }

      // The entry is meant for this configuration.
      impl.Libraries.push_back(
        cmLinkItem(name, this->FindTargetToLink(name)));
      }

    std::set<std::string> const& seenProps = cge->GetSeenTargetProperties();
    for (std::set<std::string>::const_iterator it = seenProps.begin();
        it != seenProps.end(); ++it)
      {
      if (!this->GetProperty(*it))
        {
        this->LinkImplicitNullProperties.insert(*it);
        }
      }
    cge->GetMaxLanguageStandard(this, this->MaxLanguageStandards);
    }

  cmTarget::LinkLibraryType linkType = this->ComputeLinkType(config);
  LinkLibraryVectorType const& oldllibs = this->GetOriginalLinkLibraries();
  for(cmTarget::LinkLibraryVectorType::const_iterator li = oldllibs.begin();
      li != oldllibs.end(); ++li)
    {
    if(li->second != cmTarget::GENERAL && li->second != linkType)
      {
      std::string name = this->CheckCMP0004(li->first);
      if(name == this->GetName() || name.empty())
        {
        continue;
        }
      // Support OLD behavior for CMP0003.
      impl.WrongConfigLibraries.push_back(
        cmLinkItem(name, this->FindTargetToLink(name)));
      }
    }
}

//----------------------------------------------------------------------------
void
cmTarget::ComputeLinkImplementationLanguages(const std::string& config,
                                             LinkImplementation& impl,
                                             cmTarget const* head) const
{
  // This target needs runtime libraries for its source languages.
  std::set<std::string> languages;
  // Get languages used in our source files.
  this->GetLanguages(languages, config, head);
  // Copy the set of langauges to the link implementation.
  for(std::set<std::string>::iterator li = languages.begin();
      li != languages.end(); ++li)
    {
    impl.Languages.push_back(*li);
    }
}

//----------------------------------------------------------------------------
cmTarget const* cmTarget::FindTargetToLink(std::string const& name) const
{
  cmTarget const* tgt = this->Makefile->FindTargetToUse(name);

  // Skip targets that will not really be linked.  This is probably a
  // name conflict between an external library and an executable
  // within the project.
  if(tgt && tgt->GetType() == cmTarget::EXECUTABLE &&
     !tgt->IsExecutableWithExports())
    {
    tgt = 0;
    }

  if(tgt && tgt->GetType() == cmTarget::OBJECT_LIBRARY)
    {
    cmOStringStream e;
    e << "Target \"" << this->GetName() << "\" links to "
      "OBJECT library \"" << tgt->GetName() << "\" but this is not "
      "allowed.  "
      "One may link only to STATIC or SHARED libraries, or to executables "
      "with the ENABLE_EXPORTS property set.";
    cmake* cm = this->Makefile->GetCMakeInstance();
    cm->IssueMessage(cmake::FATAL_ERROR, e.str(), this->GetBacktrace());
    tgt = 0;
    }

  // Return the target found, if any.
  return tgt;
}

//----------------------------------------------------------------------------
std::string cmTarget::CheckCMP0004(std::string const& item) const
{
  // Strip whitespace off the library names because we used to do this
  // in case variables were expanded at generate time.  We no longer
  // do the expansion but users link to libraries like " ${VAR} ".
  std::string lib = item;
  std::string::size_type pos = lib.find_first_not_of(" \t\r\n");
  if(pos != lib.npos)
    {
    lib = lib.substr(pos, lib.npos);
    }
  pos = lib.find_last_not_of(" \t\r\n");
  if(pos != lib.npos)
    {
    lib = lib.substr(0, pos+1);
    }
  if(lib != item)
    {
    cmake* cm = this->Makefile->GetCMakeInstance();
    switch(this->PolicyStatusCMP0004)
      {
      case cmPolicies::WARN:
        {
        cmOStringStream w;
        w << (this->Makefile->GetPolicies()
              ->GetPolicyWarning(cmPolicies::CMP0004)) << "\n"
          << "Target \"" << this->GetName() << "\" links to item \""
          << item << "\" which has leading or trailing whitespace.";
        cm->IssueMessage(cmake::AUTHOR_WARNING, w.str(),
                         this->GetBacktrace());
        }
      case cmPolicies::OLD:
        break;
      case cmPolicies::NEW:
        {
        cmOStringStream e;
        e << "Target \"" << this->GetName() << "\" links to item \""
          << item << "\" which has leading or trailing whitespace.  "
          << "This is now an error according to policy CMP0004.";
        cm->IssueMessage(cmake::FATAL_ERROR, e.str(), this->GetBacktrace());
        }
        break;
      case cmPolicies::REQUIRED_IF_USED:
      case cmPolicies::REQUIRED_ALWAYS:
        {
        cmOStringStream e;
        e << (this->Makefile->GetPolicies()
              ->GetRequiredPolicyError(cmPolicies::CMP0004)) << "\n"
          << "Target \"" << this->GetName() << "\" links to item \""
          << item << "\" which has leading or trailing whitespace.";
        cm->IssueMessage(cmake::FATAL_ERROR, e.str(), this->GetBacktrace());
        }
        break;
      }
    }
  return lib;
}

template<typename PropertyType>
PropertyType getLinkInterfaceDependentProperty(cmTarget const* tgt,
                                               const std::string& prop,
                                               const std::string& config,
                                               CompatibleType,
                                               PropertyType *);

template<>
bool getLinkInterfaceDependentProperty(cmTarget const* tgt,
                                       const std::string& prop,
                                       const std::string& config,
                                       CompatibleType, bool *)
{
  return tgt->GetLinkInterfaceDependentBoolProperty(prop, config);
}

template<>
const char * getLinkInterfaceDependentProperty(cmTarget const* tgt,
                                               const std::string& prop,
                                               const std::string& config,
                                               CompatibleType t,
                                               const char **)
{
  switch(t)
  {
  case BoolType:
    assert(!"String compatibility check function called for boolean");
    return 0;
  case StringType:
    return tgt->GetLinkInterfaceDependentStringProperty(prop, config);
  case NumberMinType:
    return tgt->GetLinkInterfaceDependentNumberMinProperty(prop, config);
  case NumberMaxType:
    return tgt->GetLinkInterfaceDependentNumberMaxProperty(prop, config);
  }
  assert(!"Unreachable!");
  return 0;
}

//----------------------------------------------------------------------------
template<typename PropertyType>
void checkPropertyConsistency(cmTarget const* depender,
                              cmTarget const* dependee,
                              const std::string& propName,
                              std::set<std::string> &emitted,
                              const std::string& config,
                              CompatibleType t,
                              PropertyType *)
{
  const char *prop = dependee->GetProperty(propName);
  if (!prop)
    {
    return;
    }

  std::vector<std::string> props;
  cmSystemTools::ExpandListArgument(prop, props);
  std::string pdir =
    dependee->GetMakefile()->GetRequiredDefinition("CMAKE_ROOT");
  pdir += "/Help/prop_tgt/";

  for(std::vector<std::string>::iterator pi = props.begin();
      pi != props.end(); ++pi)
    {
    std::string pname = cmSystemTools::HelpFileName(*pi);
    std::string pfile = pdir + pname + ".rst";
    if(cmSystemTools::FileExists(pfile.c_str(), true))
      {
      cmOStringStream e;
      e << "Target \"" << dependee->GetName() << "\" has property \""
        << *pi << "\" listed in its " << propName << " property.  "
          "This is not allowed.  Only user-defined properties may appear "
          "listed in the " << propName << " property.";
      depender->GetMakefile()->IssueMessage(cmake::FATAL_ERROR, e.str());
      return;
      }
    if(emitted.insert(*pi).second)
      {
      getLinkInterfaceDependentProperty<PropertyType>(depender, *pi, config,
                                                      t, 0);
      if (cmSystemTools::GetErrorOccuredFlag())
        {
        return;
        }
      }
    }
}

static std::string intersect(const std::set<std::string> &s1,
                             const std::set<std::string> &s2)
{
  std::set<std::string> intersect;
  std::set_intersection(s1.begin(),s1.end(),
                        s2.begin(),s2.end(),
                      std::inserter(intersect,intersect.begin()));
  if (!intersect.empty())
    {
    return *intersect.begin();
    }
  return "";
}
static std::string intersect(const std::set<std::string> &s1,
                       const std::set<std::string> &s2,
                       const std::set<std::string> &s3)
{
  std::string result;
  result = intersect(s1, s2);
  if (!result.empty())
    return result;
  result = intersect(s1, s3);
  if (!result.empty())
    return result;
  return intersect(s2, s3);
}
static std::string intersect(const std::set<std::string> &s1,
                       const std::set<std::string> &s2,
                       const std::set<std::string> &s3,
                       const std::set<std::string> &s4)
{
  std::string result;
  result = intersect(s1, s2);
  if (!result.empty())
    return result;
  result = intersect(s1, s3);
  if (!result.empty())
    return result;
  result = intersect(s1, s4);
  if (!result.empty())
    return result;
  return intersect(s2, s3, s4);
}

//----------------------------------------------------------------------------
void cmTarget::CheckPropertyCompatibility(cmComputeLinkInformation *info,
                                          const std::string& config) const
{
  const cmComputeLinkInformation::ItemVector &deps = info->GetItems();

  std::set<std::string> emittedBools;
  std::set<std::string> emittedStrings;
  std::set<std::string> emittedMinNumbers;
  std::set<std::string> emittedMaxNumbers;

  for(cmComputeLinkInformation::ItemVector::const_iterator li =
      deps.begin();
      li != deps.end(); ++li)
    {
    if (!li->Target)
      {
      continue;
      }

    checkPropertyConsistency<bool>(this, li->Target,
                                std::string("COMPATIBLE_INTERFACE_BOOL"),
                                emittedBools, config, BoolType, 0);
    if (cmSystemTools::GetErrorOccuredFlag())
      {
      return;
      }
    checkPropertyConsistency<const char *>(this, li->Target,
                                std::string("COMPATIBLE_INTERFACE_STRING"),
                                emittedStrings, config,
                                StringType, 0);
    if (cmSystemTools::GetErrorOccuredFlag())
      {
      return;
      }
    checkPropertyConsistency<const char *>(this, li->Target,
                                std::string("COMPATIBLE_INTERFACE_NUMBER_MIN"),
                                emittedMinNumbers, config,
                                NumberMinType, 0);
    if (cmSystemTools::GetErrorOccuredFlag())
      {
      return;
      }
    checkPropertyConsistency<const char *>(this, li->Target,
                                std::string("COMPATIBLE_INTERFACE_NUMBER_MAX"),
                                emittedMaxNumbers, config,
                                NumberMaxType, 0);
    if (cmSystemTools::GetErrorOccuredFlag())
      {
      return;
      }
    }

  std::string prop = intersect(emittedBools,
                               emittedStrings,
                               emittedMinNumbers,
                               emittedMaxNumbers);

  if (!prop.empty())
    {
    std::set<std::string> props;
    std::set<std::string>::const_iterator i = emittedBools.find(prop);
    if (i != emittedBools.end())
      {
      props.insert("COMPATIBLE_INTERFACE_BOOL");
      }
    i = emittedStrings.find(prop);
    if (i != emittedStrings.end())
      {
      props.insert("COMPATIBLE_INTERFACE_STRING");
      }
    i = emittedMinNumbers.find(prop);
    if (i != emittedMinNumbers.end())
      {
      props.insert("COMPATIBLE_INTERFACE_NUMBER_MIN");
      }
    i = emittedMaxNumbers.find(prop);
    if (i != emittedMaxNumbers.end())
      {
      props.insert("COMPATIBLE_INTERFACE_NUMBER_MAX");
      }

    std::string propsString = *props.begin();
    props.erase(props.begin());
    while (props.size() > 1)
      {
      propsString += ", " + *props.begin();
      props.erase(props.begin());
      }
   if (props.size() == 1)
     {
     propsString += " and the " + *props.begin();
     }
    cmOStringStream e;
    e << "Property \"" << prop << "\" appears in both the "
      << propsString <<
    " property in the dependencies of target \"" << this->GetName() <<
    "\".  This is not allowed. A property may only require compatibility "
    "in a boolean interpretation, a numeric minimum, a numeric maximum or a "
    "string interpretation, but not a mixture.";
    this->Makefile->IssueMessage(cmake::FATAL_ERROR, e.str());
    }
}

//----------------------------------------------------------------------------
cmComputeLinkInformation*
cmTarget::GetLinkInformation(const std::string& config) const
{
  // Lookup any existing information for this configuration.
  std::string key(cmSystemTools::UpperCase(config));
  cmTargetLinkInformationMap::iterator
    i = this->LinkInformation.find(key);
  if(i == this->LinkInformation.end())
    {
    // Compute information for this configuration.
    cmComputeLinkInformation* info =
      new cmComputeLinkInformation(this, config);
    if(!info || !info->Compute())
      {
      delete info;
      info = 0;
      }

    // Store the information for this configuration.
    cmTargetLinkInformationMap::value_type entry(key, info);
    i = this->LinkInformation.insert(entry).first;

    if (info)
      {
      this->CheckPropertyCompatibility(info, config);
      }
    }
  return i->second;
}

//----------------------------------------------------------------------------
std::string cmTarget::GetFrameworkDirectory(const std::string& config,
                                            bool rootDir) const
{
  std::string fpath;
  fpath += this->GetOutputName(config, false);
  fpath += ".framework";
  if(!rootDir)
    {
    fpath += "/Versions/";
    fpath += this->GetFrameworkVersion();
    }
  return fpath;
}

//----------------------------------------------------------------------------
std::string cmTarget::GetCFBundleDirectory(const std::string& config,
                                           bool contentOnly) const
{
  std::string fpath;
  fpath += this->GetOutputName(config, false);
  fpath += ".";
  const char *ext = this->GetProperty("BUNDLE_EXTENSION");
  if (!ext)
    {
    ext = "bundle";
    }
  fpath += ext;
  fpath += "/Contents";
  if(!contentOnly)
    fpath += "/MacOS";
  return fpath;
}

//----------------------------------------------------------------------------
std::string cmTarget::GetAppBundleDirectory(const std::string& config,
                                            bool contentOnly) const
{
  std::string fpath = this->GetFullName(config, false);
  fpath += ".app/Contents";
  if(!contentOnly)
    fpath += "/MacOS";
  return fpath;
}

//----------------------------------------------------------------------------
std::string cmTarget::BuildMacContentDirectory(const std::string& base,
                                               const std::string& config,
                                               bool contentOnly) const
{
  std::string fpath = base;
  if(this->IsAppBundleOnApple())
    {
    fpath += this->GetAppBundleDirectory(config, contentOnly);
    }
  if(this->IsFrameworkOnApple())
    {
    fpath += this->GetFrameworkDirectory(config, contentOnly);
    }
  if(this->IsCFBundleOnApple())
    {
    fpath += this->GetCFBundleDirectory(config, contentOnly);
    }
  return fpath;
}

//----------------------------------------------------------------------------
std::string cmTarget::GetMacContentDirectory(const std::string& config,
                                             bool implib) const
{
  // Start with the output directory for the target.
  std::string fpath = this->GetDirectory(config, implib);
  fpath += "/";
  bool contentOnly = true;
  if(this->IsFrameworkOnApple())
    {
    // additional files with a framework go into the version specific
    // directory
    contentOnly = false;
    }
  fpath = this->BuildMacContentDirectory(fpath, config, contentOnly);
  return fpath;
}

//----------------------------------------------------------------------------
cmTargetLinkInformationMap
::cmTargetLinkInformationMap(cmTargetLinkInformationMap const& r): derived()
{
  // Ideally cmTarget instances should never be copied.  However until
  // we can make a sweep to remove that, this copy constructor avoids
  // allowing the resources (LinkInformation) from getting copied.  In
  // the worst case this will lead to extra cmComputeLinkInformation
  // instances.  We also enforce in debug mode that the map be emptied
  // when copied.
  static_cast<void>(r);
  assert(r.empty());
}

//----------------------------------------------------------------------------
cmTargetLinkInformationMap::~cmTargetLinkInformationMap()
{
  for(derived::iterator i = this->begin(); i != this->end(); ++i)
    {
    delete i->second;
    }
}

//----------------------------------------------------------------------------
cmTargetInternalPointer::cmTargetInternalPointer()
{
  this->Pointer = new cmTargetInternals;
}

//----------------------------------------------------------------------------
cmTargetInternalPointer
::cmTargetInternalPointer(cmTargetInternalPointer const& r)
{
  // Ideally cmTarget instances should never be copied.  However until
  // we can make a sweep to remove that, this copy constructor avoids
  // allowing the resources (Internals) to be copied.
  this->Pointer = new cmTargetInternals(*r.Pointer);
}

//----------------------------------------------------------------------------
cmTargetInternalPointer::~cmTargetInternalPointer()
{
  deleteAndClear(this->Pointer->IncludeDirectoriesEntries);
  deleteAndClear(this->Pointer->CompileOptionsEntries);
  deleteAndClear(this->Pointer->CompileFeaturesEntries);
  deleteAndClear(this->Pointer->CompileDefinitionsEntries);
  deleteAndClear(this->Pointer->SourceEntries);
  delete this->Pointer;
}

//----------------------------------------------------------------------------
cmTargetInternalPointer&
cmTargetInternalPointer::operator=(cmTargetInternalPointer const& r)
{
  if(this == &r) { return *this; } // avoid warning on HP about self check
  // Ideally cmTarget instances should never be copied.  However until
  // we can make a sweep to remove that, this copy constructor avoids
  // allowing the resources (Internals) to be copied.
  cmTargetInternals* oldPointer = this->Pointer;
  this->Pointer = new cmTargetInternals(*r.Pointer);
  delete oldPointer;
  return *this;
}
