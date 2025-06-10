// $Id: RootInFile.h 1859 2021-06-11 09:27:13Z darko $
#ifndef _io_RootInFile_h_
#define _io_RootInFile_h_

/*
  GPLv2 and 2C-BSD
  Copyright (c) Darko Veberic, 2014
*/

#include <TChain.h>
#include <TChainElement.h>
#include <TFile.h>
#include <TError.h>
#include <string>
#include <limits>
#include <stdexcept>
#include <iostream>


#ifdef IO_ROOTFILE_DEBUG
#  define IO_ROOTFILE_CHECK Check()
#else
#  define IO_ROOTFILE_CHECK
#endif

namespace io {

  template<class Entry>
  class RootInFile {
  public:
    class Iterator {
    public:
      using iterator_category = std::input_iterator_tag;
      using value_type = Entry;
      using difference_type = std::ptrdiff_t;
      using pointer = Entry*;
      using reference = Entry&;

      //Iterator(const Iterator& b) : fFile(b.fFile), fIndex(b.fIndex) { }
      Iterator(RootInFile& file, const ULong64_t index) : fFile(file), fIndex(index) { }
      Iterator& operator++() { ++fIndex; return *this; } // prefix ++it
      Iterator& operator+=(const int n) { fIndex += n; return *this; }
      Entry& operator*() { return fFile[fIndex]; }
      const Entry& operator*() const { return fFile[fIndex]; }
      Entry* operator->() { return &operator*(); }
      const Entry* operator->() const { return &operator*(); }
      bool operator==(const Iterator& it) const { IsSameFile(it); return fIndex == it.fIndex; }
      bool operator!=(const Iterator& it) const { IsSameFile(it); return !operator==(it); }
    private:
      void
      IsSameFile(const Iterator& it)
        const
      {
        if (&fFile != &it.fFile)
          throw std::logic_error("RootInFile::Iterator: mixing iterators of different RootFiles");
      }

      RootInFile& fFile;
      ULong64_t fIndex;
    };

    RootInFile(const std::string& filename,
               const std::string& treeName = "",
               const std::string& branchName = "",
               const bool checkValidity = false,
               const bool skipRecovered = false,
               const bool verbose = false)
    { Open({filename}, treeName, branchName, checkValidity, skipRecovered, verbose); }

    RootInFile(const std::vector<std::string>& filenames,
               const std::string& treeName = "",
               const std::string& branchName = "",
               const bool checkValidity = false,
               const bool skipRecovered = false,
               const bool verbose = false)
    { Open(filenames, treeName, branchName, checkValidity, skipRecovered, verbose); }

    ~RootInFile() { Close(); }

    ULong64_t GetSize()
    { IO_ROOTFILE_CHECK; return fChain->GetEntries(); }

    Entry&
    operator[](const ULong64_t i)
    {
      IO_ROOTFILE_CHECK;
      if (i != fCurrentEntryIndex) {
        if (!fChain->GetEntry(i))
          throw std::out_of_range("RootInFile::operator[]: requested entry not found in file chain");
        fCurrentEntryIndex = i;
      }
      IO_ROOTFILE_CHECK;
      return *fEntryBuffer;
    }

    Iterator Begin()
    { IO_ROOTFILE_CHECK; return Iterator(*this, 0); }

    Iterator begin() { return Begin(); }

    Iterator End()
    { IO_ROOTFILE_CHECK; return Iterator(*this, GetSize()); }

    Iterator end() { return End(); }

    std::vector<Entry> ReadAll() { return std::vector<Entry>(begin(), end()); }

    template<class T>
    T&
    Get(const std::string& name = T::Class_Name())
    {
      T* obj = Find<T>(name);
      if (!obj)
        Error(("RootInFile::Get: no object '" + name + "' found in file").c_str());
      return *obj;
    }

    template<class T>
    bool Has(const std::string& name = T::Class_Name()) { return Find<T>(name); }

    void
    Close()
    {
      fCurrentEntryIndex = std::numeric_limits<decltype(fCurrentEntryIndex)>::max();
      delete fEntryBuffer;
      fEntryBuffer = nullptr;
      delete fChain;
      fChain = nullptr;
    }

    void SetBranchStatus(const std::string& branch, const bool status)
    { Check(); fChain->SetBranchStatus(branch.c_str(), status); }

    TChain& GetChain() { Check(); return *fChain; }

    static
    bool
    IsValid(const std::string& name,
            const std::string& treeName = std::string(Entry::Class_Name()) + "Tree",
            const bool rejectRecovered = false,
            const bool verbose = false)
    {
      bool isOk = true;
      auto* const file = TFile::Open(name.c_str());
      if (!file) {
        if (verbose)
          std::cerr << "RootInFile::IsValid: File '" << name << "' cannot be opened!\n";
        isOk = false;
      } else if (file->IsZombie()) {
        if (verbose)
          std::cerr << "RootInFile::IsValid: File '" << name << "' is a zombie!\n";
        isOk = false;
      } else if (rejectRecovered && file->TestBit(TFile::kRecovered)) {
        if (verbose)
          std::cerr << "RootInFile::IsValid: Reject recovered file '" << name << "'!\n";
        isOk = false;
      } else if (!file->GetListOfKeys()->Contains(treeName.c_str())) {
        if (verbose)
          std::cerr << "RootInFile::IsValid: File '" << name << "' has no TTree '" << treeName << "'!\n";
        isOk = false;
      } else {
        TTree* const tree = (TTree*)file->GetObjectChecked(treeName.c_str(), "TTree");
        if (!tree) {
          if (verbose)
            std::cerr << "RootInFile::IsValid: File '" << name << "' has no TTree '" << treeName << "'!\n";
          isOk = false;
        } else if (!tree->GetEntries()) {
          if (verbose)
            std::cerr << "RootInFile::IsValid: TTree in file '" << name << "' has no entries!\n";
          isOk = false;
        }
      }
      delete file;
      return isOk;
    }

  private:
    // prevent copying
    RootInFile(const RootInFile&);
    RootInFile& operator=(const RootInFile&);

    [[noreturn]] void Error(const char* const message)
    { Close(); throw std::runtime_error(message); }

    void
    Open(const std::vector<std::string>& filenames,
         std::string treeName,
         std::string branchName,
         const bool checkValidity, const bool skipRecovered, const bool verbose)
    {
      if (treeName.empty())
        treeName = std::string(Entry::Class_Name()) + "Tree";
      fChain = new TChain(treeName.c_str());
      size_t nFiles = 0;
      for (const auto& name : filenames) {
        if (checkValidity && !IsValid(name, treeName, skipRecovered, verbose)) {
          if (verbose)
            std::cerr << "RooInFile::Open: File '" << name << "' not valid!\n";
          continue;
        }
        nFiles += fChain->Add(name.c_str());
      }
      if (!nFiles) {
        if (verbose)
          std::cerr << "RootInFile::Open: no valid files!\n";
        delete fChain;
        fChain = nullptr;
      } else {
        fEntryBuffer = new Entry;
        if (branchName.empty())
          branchName = Entry::Class_Name();
        fChain->SetBranchAddress(branchName.c_str(), &fEntryBuffer);
        Check();
      }
    }

    template<class T>
    T*
    Find(const std::string& name)
    {
      TObjArray* files;
      if (!fChain || !(files = fChain->GetListOfFiles()))
        Error("RootInFile::Find: file not open");
      T* obj = nullptr;
      TIter next(files);
      for (TChainElement* c = (TChainElement*)next(); !obj && c; c = (TChainElement*)next())
        TFile(c->GetTitle()).GetObject(name.c_str(), obj);
      return obj;
    }

    void
    Check()
    {
      if (!fChain || !fEntryBuffer)
        Error("RootInFile::Check: no entry found");
    }

    TChain* fChain = nullptr;
    ULong64_t fCurrentEntryIndex = std::numeric_limits<decltype(fCurrentEntryIndex)>::max();
    Entry* fEntryBuffer = nullptr;
  };

}


#endif
