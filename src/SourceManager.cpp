#include <iostream>
#include <algorithm>
#include "SourceManager.h"

using namespace cmm;

void SourceManager::DumpError(LocTy L, ErrorKind K,
                              const std::string &Msg) const {
  auto LineCol = getLineColByLoc(L);

  std::cerr << (K == ErrorKind::Error ? "Error" : "Warning")
            <<" at (Line " << LineCol.first + 1 << ", Col "
            << LineCol.second + 1 << "): " << Msg << std::endl;
}

SourceManager::SourceManager(const std::string &SourcePath,
                             bool DumpInstantly)
  : SourceStream(SourcePath), DumpInstantly(DumpInstantly) {
  if (SourceStream.fail()) {
    std::cerr << "Fatal Error: Cannot open file '" << SourcePath
              << "', exited." << std::endl;
    std::exit(EXIT_FAILURE);
  }
  LineNoOffsets.reserve(ReservedLineNo);
  LineNoOffsets.emplace_back(std::streampos(0));
}

int SourceManager::get() {
  int CurChar = SourceStream.get();
  std::streampos CurPos = SourceStream.tellg();
  if (CurChar == '\n') {
    auto It = std::lower_bound(LineNoOffsets.begin(),
                               LineNoOffsets.end(),
                               CurPos);
    if (It == LineNoOffsets.cend() || *It != CurPos)
      LineNoOffsets.insert(It, CurPos);
  }
  return CurChar;
}

void SourceManager::Error(LocTy L, const std::string &Msg) {
  if (DumpInstantly)
    DumpError(L, ErrorKind::Error, Msg);
  else
    ErrorList.emplace_back(L, ErrorKind::Error, Msg);
}

void SourceManager::Error(const std::string &Msg) {
  Error(SourceStream.tellg(), Msg);
}

void SourceManager::Warning(LocTy L, const std::string &Msg) {
  if (DumpInstantly)
    DumpError(L, ErrorKind::Warning, Msg);
  else
    ErrorList.emplace_back(L, ErrorKind::Warning, Msg);
}

void SourceManager::Warning(const std::string &Msg) {
  Warning(SourceStream.tellg(), Msg);
}

std::pair<size_t, size_t> SourceManager::getLineColByLoc(LocTy L) const {
  auto It = std::upper_bound(LineNoOffsets.cbegin(),
                             LineNoOffsets.cend(), L) - 1;
  size_t LineIndex = It - LineNoOffsets.cbegin();
  size_t ColIndex = static_cast<size_t>(L - *It);
  return std::make_pair(LineIndex, ColIndex);
}