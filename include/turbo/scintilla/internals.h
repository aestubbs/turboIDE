#ifndef TURBO_SCINTILLA_INTERNALS_H
#define TURBO_SCINTILLA_INTERNALS_H

// Aggregates the Scintilla 5.x internal headers needed by turbo's platform
// layer (the Surface/Window/Font/ListBox/Menu implementation in
// source/turbo-core/platform/*.cc) and by the TScintilla subclass.
//
// The headers are resolved through the include paths added in CMakeLists.txt
// (deps/scintilla/include and deps/scintilla/src), so they are pulled straight
// from the pinned submodule rather than from a vendored copy. The include order
// mirrors Scintilla's own translation units (see src/ScintillaBase.cxx).

// C standard library
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>

// C++ standard library
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <forward_list>
#include <algorithm>
#include <memory>
#include <chrono>

// Scintilla public interface (enums, messages, structures, lexer interface)
#include <ScintillaTypes.h>
#include <ScintillaMessages.h>
#include <ScintillaStructures.h>
#include <ILoader.h>
#include <ILexer.h>

// Scintilla internal headers
#include <Debugging.h>
#include <Geometry.h>
#include <Platform.h>

#include <CharacterType.h>
#include <CharacterCategoryMap.h>
#include <Position.h>
#include <UniqueString.h>
#include <SplitVector.h>
#include <Partitioning.h>
#include <RunStyles.h>
#include <ContractionState.h>
#include <CellBuffer.h>
#include <PerLine.h>
#include <CallTip.h>
#include <KeyMap.h>
#include <Indicator.h>
#include <LineMarker.h>
#include <Style.h>
#include <ViewStyle.h>
#include <CharClassify.h>
#include <Decoration.h>
#include <CaseFolder.h>
#include <CaseConvert.h>
#include <Document.h>
#include <UniConversion.h>
#include <DBCS.h>
#include <Selection.h>
#include <PositionCache.h>
#include <EditModel.h>
#include <MarginView.h>
#include <EditView.h>
#include <Editor.h>
#include <ElapsedPeriod.h>
#include <AutoComplete.h>
#include <ScintillaBase.h>

#endif // TURBO_SCINTILLA_INTERNALS_H
