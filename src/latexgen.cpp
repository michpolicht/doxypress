/*************************************************************************
 *
 * Copyright (C) 1997-2014 by Dimitri van Heesch. 
 * Copyright (C) 2014-2015 Barbara Geller & Ansel Sermersheim 
 * All rights reserved.    
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation under the terms of the GNU General Public License version 2
 * is hereby granted. No representations are made about the suitability of
 * this software for any purpose. It is provided "as is" without express or
 * implied warranty. See the GNU General Public License for more details.
 *
 * Documents produced by Doxygen are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
*************************************************************************/

#include <QDir>

#include <stdlib.h>

#include <cite.h>
#include <classlist.h>
#include <config.h>
#include <diagram.h>
#include <dirdef.h>
#include <docparser.h>

#include <dot.h>
#include <doxy_build_info.h>
#include <doxy_globals.h>
#include <filename.h>
#include <groupdef.h>
#include <language.h>
#include <latexdocvisitor.h>
#include <latexgen.h>
#include <message.h>
#include <namespacedef.h>
#include <resourcemgr.h>
#include <pagedef.h>
#include <util.h>

LatexGenerator::LatexGenerator() : OutputGenerator()
{
   m_dir = Config::getString("latex-output");
   m_prettyCode = Config::getBool("latex-source-code");

   col = 0;
   m_indent = 0;

   insideTabbing = false;
   firstDescItem = true;
   disableLinks = false;
   templateMemberItem = false;
}

LatexGenerator::~LatexGenerator()
{
}

static void writeLatexMakefile()
{
   bool generateBib = ! Doxygen::citeDict->isEmpty();

   QByteArray dir = Config::getString("latex-output");
   QByteArray fileName = dir + "/Makefile";
   QFile file(fileName);

   if (! file.open(QIODevice::WriteOnly)) {
      err("Could not open file %s for writing\n", fileName.data());
      exit(1);
   }

   // inserted by KONNO Akihisa <konno@researchers.jp> 2002-03-05
   QByteArray latex_command = Config_getString("LATEX_CMD_NAME");
   QByteArray mkidx_command = Config_getString("MAKEINDEX_CMD_NAME");

   // end insertion by KONNO Akihisa <konno@researchers.jp> 2002-03-05
   QTextStream t(&file);

   if (! Config::getBool("latex-pdf")) { 
      // use plain old latex

      t << "all: refman.dvi" << endl
        << endl
        << "ps: refman.ps" << endl
        << endl
        << "pdf: refman.pdf" << endl
        << endl
        << "ps_2on1: refman_2on1.ps" << endl
        << endl
        << "pdf_2on1: refman_2on1.pdf" << endl
        << endl
        << "refman.ps: refman.dvi" << endl
        << "\tdvips -o refman.ps refman.dvi" << endl
        << endl;
      t << "refman.pdf: refman.ps" << endl;
      t << "\tps2pdf refman.ps refman.pdf" << endl << endl;
      t << "refman.dvi: clean refman.tex doxygen.sty" << endl
        << "\techo \"Running latex...\"" << endl
        << "\t" << latex_command << " refman.tex" << endl
        << "\techo \"Running makeindex...\"" << endl
        << "\t" << mkidx_command << " refman.idx" << endl;

      if (generateBib) {
         t << "\techo \"Running bibtex...\"" << endl;
         t << "\tbibtex refman" << endl;
         t << "\techo \"Rerunning latex....\"" << endl;
         t << "\t" << latex_command << " refman.tex" << endl;
      }

      t << "\techo \"Rerunning latex....\"" << endl
        << "\t" << latex_command << " refman.tex" << endl
        << "\tlatex_count=8 ; \\" << endl
        << "\twhile egrep -s 'Rerun (LaTeX|to get cross-references right)' refman.log && [ $$latex_count -gt 0 ] ;\\" << endl
        << "\t    do \\" << endl
        << "\t      echo \"Rerunning latex....\" ;\\" << endl
        << "\t      " << latex_command << " refman.tex ;\\" << endl
        << "\t      latex_count=`expr $$latex_count - 1` ;\\" << endl
        << "\t    done" << endl
        << "\t" << mkidx_command << " refman.idx" << endl
        << "\t" << latex_command << " refman.tex" << endl << endl
        << "refman_2on1.ps: refman.ps" << endl
        << "\tpsnup -2 refman.ps >refman_2on1.ps" << endl
        << endl
        << "refman_2on1.pdf: refman_2on1.ps" << endl
        << "\tps2pdf refman_2on1.ps refman_2on1.pdf" << endl;

   } else { // use pdflatex for higher quality output
      t << "all: refman.pdf" << endl << endl
        << "pdf: refman.pdf" << endl << endl;
      t << "refman.pdf: clean refman.tex" << endl;
      t << "\tpdflatex refman" << endl;
      t << "\t" << mkidx_command << " refman.idx" << endl;

      if (generateBib) {
         t << "\tbibtex refman" << endl;
         t << "\tpdflatex refman" << endl;
      }

      t << "\tpdflatex refman" << endl
        << "\tlatex_count=8 ; \\" << endl
        << "\twhile egrep -s 'Rerun (LaTeX|to get cross-references right)' refman.log && [ $$latex_count -gt 0 ] ;\\" << endl
        << "\t    do \\" << endl
        << "\t      echo \"Rerunning latex....\" ;\\" << endl
        << "\t      pdflatex refman ;\\" << endl
        << "\t      latex_count=`expr $$latex_count - 1` ;\\" << endl
        << "\t    done" << endl
        << "\t" << mkidx_command << " refman.idx" << endl
        << "\tpdflatex refman" << endl << endl;
   }

   t << endl
     << "clean:" << endl
     << "\trm -f "
     << "*.ps *.dvi *.aux *.toc *.idx *.ind *.ilg *.log *.out *.brf *.blg *.bbl refman.pdf" << endl;
}

static void writeMakeBat()
{

#if defined(_MSC_VER)
   QByteArray dir = Config_getString("LATEX_OUTPUT");
   QByteArray fileName = dir + "/make.bat";
   QByteArray latex_command = Config_getString("LATEX_CMD_NAME");
   QByteArray mkidx_command = Config_getString("MAKEINDEX_CMD_NAME");
   QFile file(fileName);

   bool generateBib = !Doxygen::citeDict->isEmpty();
   if (!file.open(QIODevice::WriteOnly)) {
      err("Could not open file %s for writing\n", fileName.data());
      exit(1);
   }

   QTextStream t(&file);
   t << "set Dir_Old=%cd%\n";
   t << "cd /D %~dp0\n\n";
   t << "del /s /f *.ps *.dvi *.aux *.toc *.idx *.ind *.ilg *.log *.out *.brf *.blg *.bbl refman.pdf\n\n";

   if (! Config::getBool("latex-pdf")) { 
      // use plain old latex

      t << latex_command << " refman.tex\n";
      t << "echo ----\n";
      t << mkidx_command << " refman.idx\n";
      if (generateBib) {
         t << "bibtex refman\n";
         t << "echo ----\n";
         t << latex_command << " refman.tex\n";
      }
      t << "setlocal enabledelayedexpansion\n";
      t << "set count=8\n";
      t << ":repeat\n";
      t << "set content=X\n";
      t << "for /F \"tokens=*\" %%T in ( 'findstr /C:\"Rerun LaTeX\" refman.log' ) do set content=\"%%~T\"\n";
      t << "if !content! == X for /F \"tokens=*\" %%T in ( 'findstr /C:\"Rerun to get cross-references right\" refman.log' ) do set content=\"%%~T\"\n";
      t << "if !content! == X goto :skip\n";
      t << "set /a count-=1\n";
      t << "if !count! EQU 0 goto :skip\n\n";
      t << "echo ----\n";
      t << latex_command << " refman.tex\n";
      t << "goto :repeat\n";
      t << ":skip\n";
      t << "endlocal\n";
      t << mkidx_command << " refman.idx\n";
      t << latex_command << " refman.tex\n";
      t << "dvips -o refman.ps refman.dvi\n";
      t << "gswin32c -q -dNOPAUSE -dBATCH -sDEVICE=pdfwrite "
        "-sOutputFile=refman.pdf -c save pop -f refman.ps\n";

   } else { // use pdflatex
      t << "pdflatex refman\n";
      t << "echo ----\n";
      t << mkidx_command << " refman.idx\n";
      if (generateBib) {
         t << "bibtex refman" << endl;
         t << "pdflatex refman" << endl;
      }
      t << "echo ----\n";
      t << "pdflatex refman\n\n";
      t << "setlocal enabledelayedexpansion\n";
      t << "set count=8\n";
      t << ":repeat\n";
      t << "set content=X\n";
      t << "for /F \"tokens=*\" %%T in ( 'findstr /C:\"Rerun LaTeX\" refman.log' ) do set content=\"%%~T\"\n";
      t << "if !content! == X for /F \"tokens=*\" %%T in ( 'findstr /C:\"Rerun to get cross-references right\" refman.log' ) do set content=\"%%~T\"\n";
      t << "if !content! == X goto :skip\n";
      t << "set /a count-=1\n";
      t << "if !count! EQU 0 goto :skip\n\n";
      t << "echo ----\n";
      t << "pdflatex refman\n";
      t << "goto :repeat\n";
      t << ":skip\n";
      t << "endlocal\n";
      t << mkidx_command << " refman.idx\n";
      t << "pdflatex refman\n";
      t << "cd /D %Dir_Old%\n";
      t << "set Dir_Old=\n";
   }
#endif

}

void LatexGenerator::init()
{
   QByteArray dir = Config_getString("LATEX_OUTPUT");
   QDir d(dir);

   if (! d.exists() && !d.mkdir(dir)) {
      err("Could not create output directory %s\n", dir.data());
      exit(1);
   }

   writeLatexMakefile();
   writeMakeBat();

   createSubDirs(d);
}

static void writeDefaultHeaderPart1(QTextStream &t_stream)
{
   // part 1

   // Handle batch mode
   if (Config_getBool("LATEX_BATCHMODE")) {
      t_stream << "\\batchmode\n";
   }

   // Set document class depending on configuration
   QByteArray documentClass;

   if (Config_getBool("COMPACT_LATEX")) {
      documentClass = "article";
   } else {
      documentClass = "book";
   }

   t_stream << "\\documentclass[twoside]{" << documentClass << "}\n"
     "\n";

   // Load required packages
   t_stream << "% Packages required by doxygen\n"
     "\\usepackage{fixltx2e}\n" // for \textsubscript
     "\\usepackage{calc}\n"
     "\\usepackage{doxygen}\n"
     "\\usepackage{graphicx}\n"
     "\\usepackage[utf8]{inputenc}\n"
     "\\usepackage{makeidx}\n"
     "\\usepackage{multicol}\n"
     "\\usepackage{multirow}\n"
     "\\PassOptionsToPackage{warn}{textcomp}\n"
     "\\usepackage{textcomp}\n"
     "\\usepackage[nointegrals]{wasysym}\n"
     "\\usepackage[table]{xcolor}\n"
     "\n";

   // Language support
   QByteArray languageSupport = theTranslator->latexLanguageSupportCommand();

   if (! languageSupport.isEmpty()) {
      t_stream << "% NLS support packages\n"
               << languageSupport
               << "\n";
   }

   // Define default fonts
   t_stream << "% Font selection\n"
     "\\usepackage[T1]{fontenc}\n"
     "\\usepackage[scaled=.90]{helvet}\n"
     "\\usepackage{courier}\n"
     "\\usepackage{amssymb}\n"
     "\\usepackage{sectsty}\n"
     "\\renewcommand{\\familydefault}{\\sfdefault}\n"
     "\\allsectionsfont{%\n"
     "  \\fontseries{bc}\\selectfont%\n"
     "  \\color{darkgray}%\n"
     "}\n"
     "\\renewcommand{\\DoxyLabelFont}{%\n"
     "  \\fontseries{bc}\\selectfont%\n"
     "  \\color{darkgray}%\n"
     "}\n"
     "\\newcommand{\\+}{\\discretionary{\\mbox{\\scriptsize$\\hookleftarrow$}}{}{}}\n"
     "\n";

   // Define page & text layout
   QByteArray paperName;
   QByteArray &paperType = Config_getEnum("PAPER_TYPE");

   // "a4wide" package is obsolete (see bug 563698)

   if (paperType == "a4wide") {
      paperName = "a4";
   } else {
      paperName = paperType;
   }

   t_stream << "% Page & text layout\n"
     "\\usepackage{geometry}\n"
     "\\geometry{%\n"
     "  " << paperName << "paper,%\n"
     "  top=2.5cm,%\n"
     "  bottom=2.5cm,%\n"
     "  left=2.5cm,%\n"
     "  right=2.5cm%\n"
     "}\n";

   // \sloppy is obsolete (see bug 563698)

   // Allow a bit of overflow to go unnoticed by other means
   t_stream << "\\tolerance=750\n"
     "\\hfuzz=15pt\n"
     "\\hbadness=750\n"
     "\\setlength{\\emergencystretch}{15pt}\n"
     "\\setlength{\\parindent}{0cm}\n"
     "\\setlength{\\parskip}{0.2cm}\n";

   // Redefine paragraph/subparagraph environments, using sectsty fonts
   t_stream << "\\makeatletter\n"
     "\\renewcommand{\\paragraph}{%\n"
     "  \\@startsection{paragraph}{4}{0ex}{-1.0ex}{1.0ex}{%\n"
     "    \\normalfont\\normalsize\\bfseries\\SS@parafont%\n"
     "  }%\n"
     "}\n"
     "\\renewcommand{\\subparagraph}{%\n"
     "  \\@startsection{subparagraph}{5}{0ex}{-1.0ex}{1.0ex}{%\n"
     "    \\normalfont\\normalsize\\bfseries\\SS@subparafont%\n"
     "  }%\n"
     "}\n"
     "\\makeatother\n"
     "\n";

   // Headers & footers
   QByteArray genString;
   QTextStream tg(&genString);
   filterLatexString(tg, theTranslator->trGeneratedAt(dateToString(true).toUtf8(), Config_getString("PROJECT_NAME")), false, false, false);

   t_stream << "% Headers & footers\n"
     "\\usepackage{fancyhdr}\n"
     "\\pagestyle{fancyplain}\n"
     "\\fancyhead[LE]{\\fancyplain{}{\\bfseries\\thepage}}\n"
     "\\fancyhead[CE]{\\fancyplain{}{}}\n"
     "\\fancyhead[RE]{\\fancyplain{}{\\bfseries\\leftmark}}\n"
     "\\fancyhead[LO]{\\fancyplain{}{\\bfseries\\rightmark}}\n"
     "\\fancyhead[CO]{\\fancyplain{}{}}\n"
     "\\fancyhead[RO]{\\fancyplain{}{\\bfseries\\thepage}}\n"
     "\\fancyfoot[LE]{\\fancyplain{}{}}\n"
     "\\fancyfoot[CE]{\\fancyplain{}{}}\n"
     "\\fancyfoot[RE]{\\fancyplain{}{\\bfseries\\scriptsize " << genString << " Doxygen }}\n"
     "\\fancyfoot[LO]{\\fancyplain{}{\\bfseries\\scriptsize " << genString << " Doxygen }}\n"
     "\\fancyfoot[CO]{\\fancyplain{}{}}\n"
     "\\fancyfoot[RO]{\\fancyplain{}{}}\n"
     "\\renewcommand{\\footrulewidth}{0.4pt}\n";

   if (!Config_getBool("COMPACT_LATEX")) {
      t_stream << "\\renewcommand{\\chaptermark}[1]{%\n"
        "  \\markboth{#1}{}%\n"
        "}\n";
   }

   t_stream << "\\renewcommand{\\sectionmark}[1]{%\n"
     "  \\markright{\\thesection\\ #1}%\n"
     "}\n"
     "\n";

   // ToC, LoF, LoT, bibliography, and index
   t_stream << "% Indices & bibliography\n"
     "\\usepackage{natbib}\n"
     "\\usepackage[titles]{tocloft}\n"
     "\\setcounter{tocdepth}{3}\n"
     "\\setcounter{secnumdepth}{5}\n"
     "\\makeindex\n"
     "\n";

   // User-specified packages
   QStringList &extraPackages = Config_getList("EXTRA_PACKAGES");
  
   if (! extraPackages.isEmpty()) {     
      t_stream << "% Packages requested by user\n";     
   
      for (auto pkgName : extraPackages) {         
         t_stream << "\\usepackage{" << pkgName << "}\n";        
      }
   
      t_stream << "\n";      
   }

   // Hyperlinks
   bool pdfHyperlinks = Config::getBool("latex-hyper-pdf");

   if (pdfHyperlinks) {
      t_stream << "% Hyperlinks (required, but should be loaded last)\n"
        "\\usepackage{ifpdf}\n"
        "\\ifpdf\n"
        "  \\usepackage[pdftex,pagebackref=true]{hyperref}\n"
        "\\else\n"
        "  \\usepackage[ps2pdf,pagebackref=true]{hyperref}\n"
        "\\fi\n"
        "\\hypersetup{%\n"
        "  colorlinks=true,%\n"
        "  linkcolor=blue,%\n"
        "  citecolor=blue,%\n"
        "  unicode%\n"
        "}\n"
        "\n";
   }

   // Custom commands used by the header
   t_stream << "% Custom commands\n"
     "\\newcommand{\\clearemptydoublepage}{%\n"
     "  \\newpage{\\pagestyle{empty}\\cleardoublepage}%\n"
     "}\n"
     "\n"
     "\n";

   // End of preamble, now comes the document contents
   t_stream << "%===== C O N T E N T S =====\n"
     "\n"
     "\\begin{document}\n";

   if (theTranslator->idLanguage() == "greek") {
      t_stream << "\\selectlanguage{greek}\n";
   }
   t_stream << "\n";

   // Front matter
   t_stream << "% Titlepage & ToC\n";

   bool usePDFLatex = Config::getBool("latex-pdf");
   if (pdfHyperlinks && usePDFLatex) {
      // To avoid duplicate page anchors due to reuse of same numbers for
      // the index (be it as roman numbers)
      t_stream << "\\hypersetup{pageanchor=false,\n"
               << "             bookmarks=true,\n"
               << "             bookmarksnumbered=true,\n"
               << "             pdfencoding=unicode\n"
               << "            }\n";
   }

   t_stream << "\\pagenumbering{roman}\n"
               "\\begin{titlepage}\n"
               "\\vspace*{7cm}\n"
               "\\begin{center}%\n"
               "{\\Large ";
}

static void writeDefaultHeaderPart2(QTextStream &t_stream)
{
   // part 2
   // Finalize project name

   t_stream << "}\\\\\n"
               "\\vspace*{1cm}\n"
               "{\\large ";
}

static void writeDefaultHeaderPart3(QTextStream &t_stream)
{
   // part 3
   // Finalize project number
   t_stream << " DoxyPress " << versionString << "}\\\\\n"
     "\\vspace*{0.5cm}\n"
     "{\\small " << dateToString(true) << "}\\\\\n"
     "\\end{center}\n"
     "\\end{titlepage}\n";

   bool compactLatex = Config_getBool("COMPACT_LATEX");
   if (!compactLatex) {
      t_stream << "\\clearemptydoublepage\n";
   }

   // ToC
   t_stream << "\\tableofcontents\n";
   if (! compactLatex) {
      t_stream << "\\clearemptydoublepage\n";
   }

   t_stream << "\\pagenumbering{arabic}\n";
   bool pdfHyperlinks = Config::getBool("latex-hyper-pdf");
   bool usePDFLatex   = Config::getBool("latex-pdf");

   if (pdfHyperlinks && usePDFLatex) {
      // re-enable anchors again
      t_stream << "\\hypersetup{pageanchor=true}\n";
   }
   t_stream << "\n"
     "%--- Begin generated contents ---\n";
}

static void writeDefaultStyleSheet(QTextStream &t)
{
   t << ResourceMgr::instance().getAsString("latex/doxygen.sty");
}

static void writeDefaultFooter(QTextStream &t)
{
   t << "%--- End generated contents ---\n"
     "\n";

   // Bibliography
   Doxygen::citeDict->writeLatexBibliography(t);

   // Index
   QByteArray unit;

   if (Config_getBool("COMPACT_LATEX")) {
      unit = "section";
   } else {
      unit = "chapter";
   }

   t << "% Index\n"
     "\\backmatter\n"
     "\\newpage\n"
     "\\phantomsection\n"
     "\\clearemptydoublepage\n"
     "\\addcontentsline{toc}{" << unit << "}{" << theTranslator->trRTFGeneralIndex() << "}\n"
     "\\printindex\n"
     "\n"
     "\\end{document}\n";
}

void LatexGenerator::writeHeaderFile(QFile &f)
{
   QTextStream t(&f);
   t << "% Latex header for DoxyPress " << versionString << endl;
   writeDefaultHeaderPart1(t);
   t << "Your title here";
   writeDefaultHeaderPart2(t);
   t << "Generated by";
   writeDefaultHeaderPart3(t);
}

void LatexGenerator::writeFooterFile(QFile &f)
{
   QTextStream t(&f);
   t << "% Latex footer for DoxyPress " << versionString << endl;
   writeDefaultFooter(t);
}

void LatexGenerator::writeStyleSheetFile(QFile &f)
{
   QTextStream t(&f);
   t << "% stylesheet for DoxyPress " << versionString << endl;
   writeDefaultStyleSheet(t);
}

void LatexGenerator::startFile(const char *name, const char *, const char *)
{
#if 0
   setEncoding(Config_getString("LATEX_OUTPUT_ENCODING"));
#endif
   QByteArray fileName = name;
   relPath = relativePathToRoot(fileName);
   sourceFileName = stripPath(fileName);
   if (fileName.right(4) != ".tex" && fileName.right(4) != ".sty") {
      fileName += ".tex";
   }
   startPlainFile(fileName);
}

void LatexGenerator::endFile()
{
   endPlainFile();
   sourceFileName.resize(0);
}

//void LatexGenerator::writeIndex()
//{
//  startFile("refman.tex");
//}

void LatexGenerator::startProjectNumber()
{
   m_textStream << "\\\\[1ex]\\large ";
}

static QByteArray convertToLaTeX(const QByteArray &s)
{
   QByteArray result;
   QTextStream t(&result);
   filterLatexString(t, s, false, false, false);
   return result.data();
}

void LatexGenerator::startIndexSection(IndexSections is)
{
   bool &compactLatex      = Config_getBool("COMPACT_LATEX");
   QByteArray &latexHeader = Config_getString("LATEX_HEADER");

   switch (is) {
      case isTitlePageStart:       
         if (latexHeader.isEmpty()) {
            writeDefaultHeaderPart1(m_textStream);

         } else {
            QByteArray header = fileToString(latexHeader);
            m_textStream << substituteKeywords(header, "",
                                    convertToLaTeX(Config_getString("PROJECT_NAME")),
                                    convertToLaTeX(Config_getString("PROJECT_NUMBER")),
                                    convertToLaTeX(Config_getString("PROJECT_BRIEF")));
         }
      
         break;

      case isTitlePageAuthor:
         if (latexHeader.isEmpty()) {
            writeDefaultHeaderPart2(m_textStream);
         }
         break;

      case isMainPage:
         if (compactLatex) {
            m_textStream << "\\section";
         } else {
            m_textStream << "\\chapter";
         }

         m_textStream << "{"; //Introduction}\n"
         break;
      
      case isModuleIndex:
         if (compactLatex) {
            m_textStream << "\\section";
         } else {
            m_textStream << "\\chapter";
         }

         m_textStream << "{"; //Module Index}\n"
         break;

      case isDirIndex:
         if (compactLatex) {
            m_textStream << "\\section";
         } else {
            m_textStream << "\\chapter";
         }

         m_textStream << "{"; //Directory Index}\n"
         break;

      case isNamespaceIndex:
         if (compactLatex) {
            m_textStream << "\\section";
         } else {
            m_textStream << "\\chapter";
         }

         m_textStream << "{"; //Namespace Index}\"
         break;

      case isClassHierarchyIndex:
         if (compactLatex) {
            m_textStream << "\\section";
         } else {
            m_textStream << "\\chapter";
         }

         m_textStream << "{"; //Hierarchical Index}\n"
         break;

      case isCompoundIndex:
         if (compactLatex) {
            m_textStream << "\\section";
         } else {
            m_textStream << "\\chapter";
         }

         m_textStream << "{"; //Annotated Compound Index}\n"
         break;

      case isFileIndex:
         if (compactLatex) {
            m_textStream << "\\section";
         } else {
            m_textStream << "\\chapter";
         }

         m_textStream << "{"; //Annotated File Index}\n"
         break;

      case isPageIndex:
         if (compactLatex) {
            m_textStream << "\\section";
         } else {
            m_textStream << "\\chapter";
         }

         m_textStream << "{"; //Annotated Page Index}\n"
         break;

      case isModuleDocumentation:
      {
         bool found = false;        

         for (auto gd : *Doxygen::groupSDict) {
            if (found) {
               break;
            }

            if (! gd->isReference()) {
               if (compactLatex) {
                  m_textStream << "\\section";
               } else {
                  m_textStream << "\\chapter";
               }

               m_textStream << "{"; //Module Documentation}\n";
               found = true;
            }
         }
      }
      break;

      case isDirDocumentation: 
      {         
         bool found = false;

         for (auto dd : Doxygen::directories) {
            if (found) {
               break;
            }

            if (dd->isLinkableInProject()) {
               if (compactLatex) {
                  m_textStream << "\\section";
               } else {
                  m_textStream << "\\chapter";
               }
               m_textStream << "{"; //Module Documentation}\n";
               found = true;
            }
         }
      }
      break;

      case isNamespaceDocumentation: 
      {        
         bool found = false;

         for (auto nd : *Doxygen::namespaceSDict) {
            if (found) {
               break;
            }

            if (nd->isLinkableInProject()) {
               if (compactLatex) {
                  m_textStream << "\\section";
               } else {
                  m_textStream << "\\chapter";
               }
               m_textStream << "{"; // Namespace Documentation}\n":
               found = true;
            }
         }
      }
      break;

      case isClassDocumentation: 
      {        
         bool found = false;
       
         for (auto cd : *Doxygen::classSDict) {
            if (found) {
               break;
            }

            if (cd->isLinkableInProject() && cd->templateMaster() == 0 && !cd->isEmbeddedInOuterScope()) {
               if (compactLatex) {
                  m_textStream << "\\section";
               } else {
                  m_textStream << "\\chapter";
               }
               m_textStream << "{"; //Compound Documentation}\n";
               found = true;
            }
         }
      }
      break;

      case isFileDocumentation: 
      {
         bool isFirst = true;
      
         for (auto fn : *Doxygen::inputNameList) {           

            for (auto fd : *fn) { 

               if (fd->isLinkableInProject()) {
                  if (isFirst) {
                     if (compactLatex) {
                        m_textStream << "\\section";
                     } else {
                        m_textStream << "\\chapter";
                     }

                     m_textStream << "{"; //File Documentation}\n";
                     isFirst = false;
                     break;
                  }
               }
            }
         }
      }
      break;

      case isExampleDocumentation: 

         if (compactLatex) {
            m_textStream << "\\section";
         } else {
            m_textStream << "\\chapter";
         }

         m_textStream << "{"; //Example Documentation}\n";
         break;

      case isPageDocumentation: 
         if (compactLatex) {
            m_textStream << "\\section";
         } else {
            m_textStream << "\\chapter";
         }

         m_textStream << "{"; //Page Documentation}\n";      
         break;

      case isPageDocumentation2:
         break;

      case isEndIndex:
         break;
   }
}

void LatexGenerator::endIndexSection(IndexSections is)
{
   //static bool compactLatex    = Config_getBool("COMPACT_LATEX");
   static bool sourceBrowser     = Config_getBool("SOURCE_BROWSER");
   static QByteArray latexHeader = Config_getString("LATEX_HEADER");
   static QByteArray latexFooter = Config_getString("LATEX_FOOTER");

   switch (is) {
      case isTitlePageStart:
         break;

      case isTitlePageAuthor:
         if (latexHeader.isEmpty()) {
            writeDefaultHeaderPart3(m_textStream);
         }
         break;

      case isMainPage: 
      {
         //QByteArray indexName=Config_getBool("GENERATE_TREEVIEW")?"main":"index";
         QByteArray indexName = "index";
         m_textStream << "}\n\\label{index}";

         if (Config::getBool("latex-hyper-pdf")) {
            m_textStream << "\\hypertarget{index}{}";
         }
         m_textStream << "\\input{" << indexName << "}\n";
      }
      break;

      case isModuleIndex:
         m_textStream << "}\n\\input{modules}\n";
         break;
      case isDirIndex:
         m_textStream << "}\n\\input{dirs}\n";
         break;
      case isNamespaceIndex:
         m_textStream << "}\n\\input{namespaces}\n";
         break;
      case isClassHierarchyIndex:
         m_textStream << "}\n\\input{hierarchy}\n";
         break;
      case isCompoundIndex:
         m_textStream << "}\n\\input{annotated}\n";
         break;
      case isFileIndex:
         m_textStream << "}\n\\input{files}\n";
         break;
      case isPageIndex:
         m_textStream << "}\n\\input{pages}\n";
         break;

      case isModuleDocumentation: 
      {        
         bool found = false;

         for (auto gd : *Doxygen::groupSDict) {           

            if (! gd->isReference()) {
               if (! found) {
                  m_textStream << "}\n\\input{" << gd->getOutputFileBase() << "}\n";
                  found = true;
                       
               }  else {                   
                  m_textStream << "\\include";
                  m_textStream << "{" << gd->getOutputFileBase() << "}\n";
               }
            }
         }
      }
      break;

      case isDirDocumentation: 
      {         
         bool found = false;
         
         for (auto dd : Doxygen::directories) {
 
            if (dd->isLinkableInProject())  {
               if (! found) {
                  m_textStream << "}\n\\input{" << dd->getOutputFileBase() << "}\n";
                  found = true;
              
               }  else  {              
                  m_textStream << "\\input";
                  m_textStream << "{" << dd->getOutputFileBase() << "}\n";
               }
            }
         }
      }
      break;

      case isNamespaceDocumentation: 
      {
         bool found = false;

         for (auto nd : *Doxygen::namespaceSDict) {
        
            if (nd->isLinkableInProject()) {

               if (! found) { 
                  m_textStream << "}\n\\input{" << nd->getOutputFileBase() << "}\n";
                  found = true;

               } else {    
                  m_textStream << "\\input";
                  m_textStream << "{" << nd->getOutputFileBase() << "}\n";
               }
            }            
         }
      }
      break;

      case isClassDocumentation: 
      {
         bool found = false;

         for (auto cd : *Doxygen::classSDict) {
         
            if (cd->isLinkableInProject() && cd->templateMaster() == 0 && ! cd->isEmbeddedInOuterScope()) {

               if (! found) {
                  m_textStream << "}\n\\input{" << cd->getOutputFileBase() << "}\n";
                  found = true;

               } else {                     
                  m_textStream << "\\input";
                  m_textStream << "{" << cd->getOutputFileBase() << "}\n";
               }
            }
         }
      }
      break;

      case isFileDocumentation: {
         bool isFirst = true;
        
         for (auto fn : *Doxygen::inputNameList) {

            for (auto fd : *fn) {
         
               if (fd->isLinkableInProject()) {
                  if (isFirst) {
                     m_textStream << "}\n\\input{" << fd->getOutputFileBase() << "}\n";

                     if (sourceBrowser && m_prettyCode && fd->generateSourceFile()) {                        
                        m_textStream << "\\input{" << fd->getSourceFileBase() << "}\n";
                     }

                     isFirst = false;

                  } else {                
                     m_textStream << "\\input" ;
                     m_textStream << "{" << fd->getOutputFileBase() << "}\n";

                     if (sourceBrowser && m_prettyCode && fd->generateSourceFile()) {                       
                        m_textStream << "\\input{" << fd->getSourceFileBase() << "}\n";
                     }
                  }
               }
            }
         }
      }
      break;

      case isExampleDocumentation: {
         m_textStream << "}\n";

         bool isFirst = true;          

         for (auto pd : *Doxygen::exampleSDict) {

            if (isFirst) {
               m_textStream << "\\input{" << pd->getOutputFileBase() << "}\n";
               isFirst = false;

            } else {
               m_textStream << "\\input";
               m_textStream << "{" << pd->getOutputFileBase() << "}\n";
            }
         }
      }
      break;

      case isPageDocumentation: {
         m_textStream << "}\n";


#if 0
         PageSDict::Iterator pdi(*Doxygen::pageSDict);
         PageDef *pd = pdi.toFirst();
         bool first = true;

         for (pdi.toFirst(); (pd = pdi.current()); ++pdi) {

            if (!pd->getGroupDef() && !pd->isReference()) {
               if (compactLatex) {
                  m_textStream << "\\section";
               } else {
                  m_textStream << "\\chapter";
               }
               m_textStream << "{" << pd->title();
               m_textStream << "}\n";

               if (compactLatex || first) {
                  m_textStream << "\\input" ;
               } else {
                  m_textStream << "\\include";
               }
               m_textStream << "{" << pd->getOutputFileBase() << "}\n";
               first = false;
            }
         }
#endif

      }
      break;

      case isPageDocumentation2:
         break;

      case isEndIndex:
         if (latexFooter.isEmpty()) {
            writeDefaultFooter(m_textStream);
         } else {
            QByteArray footer = fileToString(latexFooter);
            m_textStream << substituteKeywords(footer, "",
                                    convertToLaTeX(Config_getString("PROJECT_NAME")),
                                    convertToLaTeX(Config_getString("PROJECT_NUMBER")),
                                    convertToLaTeX(Config_getString("PROJECT_BRIEF")));
         }
         break;
   }
}

void LatexGenerator::writePageLink(const char *name, bool)
{  
   m_textStream << "\\input" ;
   m_textStream << "{" << name << "}\n";
}


void LatexGenerator::writeStyleInfo(int part)
{
   if (part > 0) {
      return;
   }

   startPlainFile("doxygen.sty");
   writeDefaultStyleSheet(m_textStream);
   endPlainFile();
}

void LatexGenerator::newParagraph()
{
   m_textStream << endl << endl;
}

void LatexGenerator::startParagraph()
{
   m_textStream << endl << endl;
}

void LatexGenerator::endParagraph()
{
   m_textStream << endl << endl;
}

void LatexGenerator::writeString(const char *text)
{
   m_textStream << text;
}

void LatexGenerator::startIndexItem(const QByteArray &ref, const QByteArray &fn)
{
   m_textStream << "\\item ";

   if (ref.isEmpty() && ! fn.isEmpty()) {
      m_textStream << "\\contentsline{section}{";
   }
}

void LatexGenerator::endIndexItem(const QByteArray &ref, const QByteArray &fn)
{
   if (ref.isEmpty() && ! fn.isEmpty()) {
      m_textStream << "}{\\pageref{" << stripPath(fn) << "}}{}" << endl;
   }
}

//void LatexGenerator::writeIndexFileItem(const char *,const char *text)
//{
//  m_textStream << "\\item\\contentsline{section}{";
//  docify(text);
//  m_textStream << "}{\\pageref{" << text << "}}" << endl;
//}


void LatexGenerator::startHtmlLink(const QByteArray &url)
{
   if (Config::getBool("latex-hyper-pdf")) {
      m_textStream << "\\href{";
      m_textStream << url;
      m_textStream << "}";
   }

   m_textStream << "{\\tt ";
}

void LatexGenerator::endHtmlLink()
{
   m_textStream << "}";
}

//void LatexGenerator::writeMailLink(const char *url)
//{
//  if (Config::getBool("latex-hyper-pdf")) {  
//    m_textStream << "\\href{mailto:";
//    m_textStream << url;
//    m_textStream << "}";
//  }
//  m_textStream << "{\\tt ";
//  docify(url);
//  m_textStream << "}";
//}

void LatexGenerator::writeStartAnnoItem(const char *, const QByteArray &, const QByteArray &path, const char *name)
{
   m_textStream << "\\item\\contentsline{section}{\\bf ";

   if (! path.isEmpty()) {
      docify(path);
   }

   docify(name);
   m_textStream << "} ";
}

void LatexGenerator::writeEndAnnoItem(const char *name)
{
   m_textStream << "}{\\pageref{" << name << "}}{}" << endl;
}

void LatexGenerator::startIndexKey()
{
   m_textStream << "\\item\\contentsline{section}{";
}

void LatexGenerator::endIndexKey()
{
}

void LatexGenerator::startIndexValue(bool hasBrief)
{
   m_textStream << " ";

   if (hasBrief) {
      m_textStream << "\\\\*";
   }
}

void LatexGenerator::endIndexValue(const char *name, bool)
{
   m_textStream << "}{\\pageref{" << name << "}}{}" << endl;
}

//void LatexGenerator::writeClassLink(const char *,const char *, const char *,const char *name)
//{
//  m_textStream << "{\\bf ";
//  docify(name);
//  m_textStream << "}";
//}

void LatexGenerator::startTextLink(const QByteArray &file, const QByteArray &anchor)
{
   if (! disableLinks && Config::getBool("latex-hyper-pdf")) {
      m_textStream << "\\hyperlink{";

      if (! file.isEmpty()) {
         m_textStream << stripPath(file);
      }

      if (! anchor.isEmpty()) {
         m_textStream << "_" << anchor;
      }

      m_textStream << "}{";

   } else {
      m_textStream << "{\\bf ";
   }
}

void LatexGenerator::endTextLink()
{
   m_textStream << "}";
}

void LatexGenerator::writeObjectLink(const QByteArray &ref, const QByteArray &file, const QByteArray &anchor, const QByteArray &text)
{
   if (! disableLinks && ref.isEmpty() && Config::getBool("latex-hyper-pdf")) {
      m_textStream << "\\hyperlink{";

      if (! file.isEmpty()) {
         m_textStream << stripPath(file);
      }

      if (! file.isEmpty() && ! anchor.isEmpty()) {
         m_textStream << "_";
      }

      if (! anchor.isEmpty()) {
         m_textStream << anchor;
      }

      m_textStream << "}{";
      docify(text);
      m_textStream << "}";

   } else {
      m_textStream << "{\\bf ";
      docify(text);
      m_textStream << "}";
   }
}

void LatexGenerator::startPageRef()
{
   m_textStream << " \\doxyref{}{";
}

void LatexGenerator::endPageRef(const QByteArray &clname, const QByteArray &anchor)
{
   m_textStream << "}{";

   if (! clname.isEmpty()) {
      m_textStream << clname;
   }
   if (! anchor.isEmpty()) {
      m_textStream << "_" << anchor;
   }

   m_textStream << "}";
}

void LatexGenerator::writeCodeLink(const QByteArray &ref, const QByteArray &file, const QByteArray &anchor, 
                                   const QByteArray &name, const QByteArray &)
{
   static bool pdfHyperlinks = Config::getBool("latex-hyper-pdf");
   static bool usePDFLatex   = Config::getBool("latex-pdf");
   int l = qstrlen(name);

   if (col + l > 80) {
      m_textStream << "\n      ";
      col = 0;
   }

   if (/*m_prettyCode &&*/ ! disableLinks && ref.isEmpty() && usePDFLatex && pdfHyperlinks) {
      m_textStream << "\\hyperlink{";

      if (! file.isEmpty()) {
         m_textStream << stripPath(file);
      }

      if (! file.isEmpty() && ! anchor.isEmpty()) {
         m_textStream << "_";
      }

      if (! anchor.isEmpty()) {
         m_textStream << anchor;
      }

      m_textStream << "}{";
      codify(name);
      m_textStream << "}";

   } else {
      m_textStream << name;
   }

   col += l;
}

void LatexGenerator::startTitleHead(const char *fileName)
{
   static bool pdfHyperlinks = Config::getBool("latex-hyper-pdf");
   static bool usePDFLatex   = Config::getBool("latex-pdf");

   if (usePDFLatex && pdfHyperlinks && fileName) {
      m_textStream << "\\hypertarget{" << stripPath(fileName) << "}{}";
   }

   if (Config_getBool("COMPACT_LATEX")) {
      m_textStream << "\\subsection{";
   } else {
      m_textStream << "\\section{";
   }
}

void LatexGenerator::endTitleHead(const char *fileName, const char *name)
{
   m_textStream << "}" << endl;

   if (name) {
      m_textStream << "\\label{" << stripPath(fileName) << "}\\index{";
      escapeLabelName(name);

      m_textStream << "@{";
      escapeMakeIndexChars(name);

      m_textStream << "}}" << endl;
   }
}

void LatexGenerator::startTitle()
{
   if (Config_getBool("COMPACT_LATEX")) {
      m_textStream << "\\subsection{";
   } else {
      m_textStream << "\\section{";
   }
}

void LatexGenerator::startGroupHeader(int extraIndentLevel)
{
   if (Config_getBool("COMPACT_LATEX")) {
      extraIndentLevel++;
   }

   if (extraIndentLevel == 3) {
      m_textStream << "\\subparagraph*{";
   } else if (extraIndentLevel == 2) {
      m_textStream << "\\paragraph{";
   } else if (extraIndentLevel == 1) {
      m_textStream << "\\subsubsection{";
   } else { // extraIndentLevel==0
      m_textStream << "\\subsection{";
   }

   disableLinks = true;
}

void LatexGenerator::endGroupHeader(int)
{
   disableLinks = false;
   m_textStream << "}" << endl;
}

void LatexGenerator::startMemberHeader(const char *)
{
   if (Config_getBool("COMPACT_LATEX")) {
      m_textStream << "\\subsubsection*{";
   } else {
      m_textStream << "\\subsection*{";
   }
   disableLinks = true;
}

void LatexGenerator::endMemberHeader()
{
   disableLinks = false;
   m_textStream << "}" << endl;
}

void LatexGenerator::startMemberDoc(const char *clname, const char *memname, const char *, const char *title, bool showInline)
{
   if (memname && memname[0] != '@') {
      m_textStream << "\\index{";
      if (clname) {
         escapeLabelName(clname);
         m_textStream << "@{";
         escapeMakeIndexChars(clname);
         m_textStream << "}!";
      }

      escapeLabelName(memname);
      m_textStream << "@{";
      escapeMakeIndexChars(memname);
      m_textStream << "}}" << endl;

      m_textStream << "\\index{";
      escapeLabelName(memname);
      m_textStream << "@{";
      escapeMakeIndexChars(memname);
      m_textStream << "}";

      if (clname) {
         m_textStream << "!";
         escapeLabelName(clname);
         m_textStream << "@{";
         escapeMakeIndexChars(clname);
         m_textStream << "}";
      }
      m_textStream << "}" << endl;
   }

   static const char *levelLab[] = { "subsubsection", "paragraph", "subparagraph", "subparagraph" };
   static bool compactLatex = Config_getBool("COMPACT_LATEX");
   int level = 0;

   if (showInline) {
      level += 2;
   }
   if (compactLatex) {
      level++;
   }

   m_textStream << "\\" << levelLab[level];

   //if (Config::getBool("latex-hyper-pdf") && memname)
   //{
   //  m_textStream << "[";
   //  escapeMakeIndexChars(this,t,memname);
   //  m_textStream << "]";
   //}

   m_textStream << "[{";
   escapeMakeIndexChars(title);

   m_textStream << "}]";
   m_textStream << "{\\setlength{\\rightskip}{0pt plus 5cm}";

   disableLinks = true;
}

void LatexGenerator::endMemberDoc(bool)
{
   disableLinks = false;
   m_textStream << "}";
}

void LatexGenerator::startDoxyAnchor(const char *fName, const char *, const char *anchor, const char *, const char *)
{
   static bool pdfHyperlinks = Config::getBool("latex-hyper-pdf");
   static bool usePDFLatex   = Config::getBool("latex-pdf");

   if (usePDFLatex && pdfHyperlinks) {
      m_textStream << "\\hypertarget{";
      if (fName) {
         m_textStream << stripPath(fName);
      }
      if (anchor) {
         m_textStream << "_" << anchor;
      }
      m_textStream << "}{}";
   }
}

void LatexGenerator::endDoxyAnchor(const char *fName, const char *anchor)
{
   m_textStream << "\\label{";
   if (fName) {
      m_textStream << stripPath(fName);
   }
   if (anchor) {
      m_textStream << "_" << anchor;
   }
   m_textStream << "}" << endl;
}

void LatexGenerator::writeAnchor(const char *fName, const char *name)
{   
   m_textStream << "\\label{" << name << "}" << endl;

   static bool pdfHyperlinks = Config::getBool("latex-hyper-pdf");
   static bool usePDFLatex   = Config::getBool("latex-pdf");

   if (usePDFLatex && pdfHyperlinks) {
      if (fName) {
         m_textStream << "\\hypertarget{" << stripPath(fName) << "_" << name << "}{}" << endl;
      } else {
         m_textStream << "\\hypertarget{" << name << "}{}" << endl;
      }
   }
}


//void LatexGenerator::writeLatexLabel(const char *clName,const char *anchor)
//{
//  writeDoxyAnchor(0,clName,anchor,0);
//}

void LatexGenerator::addIndexItem(const char *s1, const char *s2)
{
   if (s1) {
      m_textStream << "\\index{";
      escapeLabelName(s1);
      m_textStream << "@{";
      escapeMakeIndexChars(s1);
      m_textStream << "}";

      if (s2) {
         m_textStream << "!";
         escapeLabelName(s2);
         m_textStream << "@{";
         escapeMakeIndexChars(s2);
         m_textStream << "}";
      }

      m_textStream << "}";
   }
}


void LatexGenerator::startSection(const char *lab, const char *, SectionInfo::SectionType type)
{
   static bool pdfHyperlinks = Config::getBool("latex-hyper-pdf");
   static bool usePDFLatex   = Config::getBool("latex-pdf");

   if (usePDFLatex && pdfHyperlinks) {
      m_textStream << "\\hypertarget{" << stripPath(lab) << "}{}";
   }

   m_textStream << "\\";

   if (Config_getBool("COMPACT_LATEX")) {
      switch (type) {
         case SectionInfo::Page:
            m_textStream << "subsection";
            break;
         case SectionInfo::Section:
            m_textStream << "subsubsection";
            break;
         case SectionInfo::Subsection:
            m_textStream << "paragraph";
            break;
         case SectionInfo::Subsubsection:
            m_textStream << "subparagraph";
            break;
         case SectionInfo::Paragraph:
            m_textStream << "subparagraph";
            break;
         default:
            assert(0);
            break;
      }

      m_textStream << "{";

   } else {
      switch (type) {
         case SectionInfo::Page:
            m_textStream << "section";
            break;
         case SectionInfo::Section:
            m_textStream << "subsection";
            break;
         case SectionInfo::Subsection:
            m_textStream << "subsubsection";
            break;
         case SectionInfo::Subsubsection:
            m_textStream << "paragraph";
            break;
         case SectionInfo::Paragraph:
            m_textStream << "subparagraph";
            break;
         default:
            assert(0);
            break;
      }
      m_textStream << "{";
   }
}

void LatexGenerator::endSection(const char *lab, SectionInfo::SectionType)
{
   m_textStream << "}\\label{" << lab << "}" << endl;
}

void LatexGenerator::docify(const QByteArray &text)
{
   filterLatexString(m_textStream, text, insideTabbing, false, false);
}

void LatexGenerator::codify(const QByteArray &text)
{
   if (! text.isEmpty()) {
      const char *p = text;
      char c;    

      int spacesToNextTabStop;

      static int tabSize = Config::getInt("tab-size");
      const int maxLineLen = 108;

      QByteArray result; 

      // worst case for 1 line of 4-byte chars
      result.resize(4 * maxLineLen + 1);

      int i;

      while ((c = *p)) {
         switch (c) {
            case 0x0c:
               p++;  // remove ^L
               break;

            case '\t':
               spacesToNextTabStop = tabSize - (col % tabSize);
               m_textStream << QString(spacesToNextTabStop, QChar(' '));
               col += spacesToNextTabStop;

               p++;
               break;

            case '\n':
               m_textStream << '\n';
               col = 0;
               p++;
               break;

            default:
               i = 0;

#undef  COPYCHAR
               // helper macro to copy a single utf8 character, dealing with multibyte chars.
#define COPYCHAR() do {                                           \
                     result[i++]=c; p++;                          \
                     if (c<0) /* multibyte utf-8 character */     \
                     {                                            \
                       /* 1xxx.xxxx: >=2 byte character */        \
                       result[i++]=*p++;                          \
                       if (((uchar)c&0xE0)==0xE0)                 \
                       {                                          \
                         /* 111x.xxxx: >=3 byte character */      \
                         result[i++]=*p++;                        \
                       }                                          \
                       if (((uchar)c&0xF0)==0xF0)                 \
                       {                                          \
                         /* 1111.xxxx: 4 byte character */        \
                         result[i++]=*p++;                        \
                       }                                          \
                     }                                            \
                     col++;                                       \
                   } while(0)

               // gather characters until we find whitespace or are at
               // the end of a line
               COPYCHAR();

               if (col >= maxLineLen) { // force line break
                   m_textStream << "\n      ";
                  col = 0;

               } else { // copy more characters
                  while (col < maxLineLen && (c = *p) && c != 0x0c && c != '\t' && c != '\n' && c != ' ') {
                     COPYCHAR();
                  }

                  if (col >= maxLineLen) { // force line break
                     m_textStream << "\n      ";
                     col = 0;
                  }
               }

               result[i] = 0; // add terminator

               //if (m_prettyCode)
               //{

               filterLatexString( m_textStream, result, insideTabbing, true);

               //}
               //else
               //{
               //  m_textStream << result;
               //}

               break;
         }
      }
   }
}

void LatexGenerator::writeChar(char c)
{
   char cs[2];
   cs[0] = c;
   cs[1] = 0;
   docify(cs);
}

void LatexGenerator::startClassDiagram()
{
   //if (Config_getBool("COMPACT_LATEX")) m_textStream << "\\subsubsection"; else m_textStream << "\\subsection";
   //m_textStream << "{";
}

void LatexGenerator::endClassDiagram(const ClassDiagram &d, const char *fname, const char *)
{
   d.writeFigure(m_textStream, m_dir, fname);
}


void LatexGenerator::startAnonTypeScope(int indent)
{
   if (indent == 0) {
      m_textStream << "\\begin{tabbing}" << endl;
      m_textStream << "xx\\=xx\\=xx\\=xx\\=xx\\=xx\\=xx\\=xx\\=xx\\=\\kill" << endl;
      insideTabbing = true;
   }

   m_indent = indent;
}

void LatexGenerator::endAnonTypeScope(int indent)
{
   if (indent == 0) {
      m_textStream << endl << "\\end{tabbing}";
      insideTabbing = false;
   }
   m_indent = indent;
}

void LatexGenerator::startMemberTemplateParams()
{
   if (templateMemberItem) {
      m_textStream << "{\\footnotesize ";
   }
}

void LatexGenerator::endMemberTemplateParams(const char *, const QByteArray &)
{
   if (templateMemberItem) {
      m_textStream << "}\\\\";
   }
}

void LatexGenerator::startMemberItem(const char *, int annoType, const QByteArray &)
{
   if (! insideTabbing) {
      m_textStream << "\\item " << endl;
      templateMemberItem = (annoType == 3);
   }
}

void LatexGenerator::endMemberItem()
{
   if (insideTabbing) {
      m_textStream << "\\\\";
   }
   templateMemberItem = false;
   m_textStream << endl;
}

void LatexGenerator::startMemberDescription(const char *, const QByteArray &)
{
   if (!insideTabbing) {
      m_textStream << "\\begin{DoxyCompactList}\\small\\item\\em ";

   } else {
      for (int i = 0; i < m_indent + 2; i++) {
         m_textStream << "\\>";
      }
      m_textStream << "{\\em ";
   }
}

void LatexGenerator::endMemberDescription()
{
   if (! insideTabbing) {
      m_textStream << "\\end{DoxyCompactList}";
   } else {
      m_textStream << "}\\\\\n";
   }
}

void LatexGenerator::writeNonBreakableSpace(int)
{   
   if (insideTabbing) {
      m_textStream << "\\>";
   } else {
      m_textStream << "~";
   }
}

void LatexGenerator::startMemberList()
{
   if (!insideTabbing) {
      m_textStream << "\\begin{DoxyCompactItemize}" << endl;
   }
}

void LatexGenerator::endMemberList()
{
   //printf("LatexGenerator::endMemberList(%d)\n",insideTabbing);
   if (! insideTabbing) {
      m_textStream << "\\end{DoxyCompactItemize}"   << endl;
   }
}

void LatexGenerator::startMemberGroupHeader(bool hasHeader)
{
   if (hasHeader) {
      m_textStream << "\\begin{Indent}";
   }

   m_textStream << "{\\bf ";

   // changed back to rev 756 due to bug 660501
   //if (Config_getBool("COMPACT_LATEX"))
   //{
   //  m_textStream << "\\subparagraph*{";
   //}
   //else
   //{
   //  m_textStream << "\\paragraph*{";
   //}
}

void LatexGenerator::endMemberGroupHeader()
{
   // changed back to rev 756 due to bug 660501
   m_textStream << "}\\par" << endl;
   //t << "}" << endl;
}

void LatexGenerator::startMemberGroupDocs()
{
   m_textStream << "{\\em ";
}

void LatexGenerator::endMemberGroupDocs()
{
   m_textStream << "}";
}

void LatexGenerator::startMemberGroup()
{
}

void LatexGenerator::endMemberGroup(bool hasHeader)
{
   if (hasHeader) {
      m_textStream << "\\end{Indent}";
   }
   m_textStream << endl;
}

void LatexGenerator::startDotGraph()
{
   newParagraph();
}

void LatexGenerator::endDotGraph(const DotClassGraph &g)
{
   g.writeGraph(m_textStream, GOF_EPS, EOF_LaTeX, Config_getString("LATEX_OUTPUT"), fileName, relPath);
}

void LatexGenerator::startInclDepGraph()
{
}

void LatexGenerator::endInclDepGraph(const DotInclDepGraph &g)
{
   g.writeGraph(m_textStream, GOF_EPS, EOF_LaTeX, Config_getString("LATEX_OUTPUT"), fileName, relPath);
}

void LatexGenerator::startGroupCollaboration()
{
}

void LatexGenerator::endGroupCollaboration(const DotGroupCollaboration &g)
{
   g.writeGraph(m_textStream, GOF_EPS, EOF_LaTeX, Config_getString("LATEX_OUTPUT"), fileName, relPath);
}

void LatexGenerator::startCallGraph()
{
}

void LatexGenerator::endCallGraph(const DotCallGraph &g)
{
   g.writeGraph(m_textStream, GOF_EPS, EOF_LaTeX, Config_getString("LATEX_OUTPUT"), fileName, relPath);
}

void LatexGenerator::startDirDepGraph()
{
}

void LatexGenerator::endDirDepGraph(const DotDirDeps &g)
{
   g.writeGraph(m_textStream, GOF_EPS, EOF_LaTeX, Config_getString("LATEX_OUTPUT"), fileName, relPath);
}

void LatexGenerator::startDescription()
{
   m_textStream << "\\begin{description}" << endl;
}

void LatexGenerator::endDescription()
{
   m_textStream << "\\end{description}" << endl;
   firstDescItem = true;
}

void LatexGenerator::startDescItem()
{
   firstDescItem = true;
   m_textStream << "\\item[";
}

void LatexGenerator::endDescItem()
{
   if (firstDescItem) {
      m_textStream << "]" << endl;
      firstDescItem = false;
   } else {
      lineBreak();
   }
}

void LatexGenerator::startSimpleSect(SectionTypes, const QByteArray &file, const char *anchor, const char *title)
{
   m_textStream << "\\begin{Desc}\n\\item[";

   if (! file.isEmpty()) {
      writeObjectLink(0, file, anchor, title);
   } else {
      docify(title);
   }
   m_textStream << "]";
}

void LatexGenerator::endSimpleSect()
{
   m_textStream << "\\end{Desc}" << endl;
}

void LatexGenerator::startParamList(ParamListTypes, const char *title)
{
   m_textStream << "\\begin{Desc}\n\\item[";
   docify(title);
   m_textStream << "]";
}

void LatexGenerator::endParamList()
{
   m_textStream << "\\end{Desc}" << endl;
}

void LatexGenerator::startParameterList(bool openBracket)
{
   /* start of ParameterType ParameterName list */
   if (openBracket) {
      m_textStream << "(";
   }

   m_textStream << endl << "\\begin{DoxyParamCaption}" << endl;
}

void LatexGenerator::endParameterList()
{
}

void LatexGenerator::startParameterType(bool first, const QByteArray &key)
{
   m_textStream << "\\item[{";

   if (! first && ! key.isEmpty()) {
      m_textStream << key;
   }
}

void LatexGenerator::endParameterType()
{
   m_textStream << "}]";
}

void LatexGenerator::startParameterName(bool)
{
   m_textStream << "{";
}

void LatexGenerator::endParameterName(bool last, bool, bool closeBracket)
{
   m_textStream << "}" << endl;

   if (last) {
      m_textStream << "\\end{DoxyParamCaption}" << endl;
      if (closeBracket) {
         m_textStream << ")";
      }
   }
}

void LatexGenerator::exceptionEntry(const QByteArray &prefix, bool closeBracket)
{
   if (! prefix.isEmpty()) {
      m_textStream << " " << prefix;

   } else if (closeBracket) {
      m_textStream << ")";

   }

   m_textStream << " ";
}

void LatexGenerator::writeDoc(DocNode *n, QSharedPointer<Definition> ctx, QSharedPointer<MemberDef> )
{
   LatexDocVisitor *visitor = new LatexDocVisitor(m_textStream, *this, ctx ? ctx->getDefFileExtension() : QByteArray(""), insideTabbing);
   n->accept(visitor);
   delete visitor;
}

void LatexGenerator::startConstraintList(const char *header)
{
   m_textStream << "\\begin{Desc}\n\\item[";
   docify(header);
   m_textStream << "]";
   m_textStream << "\\begin{description}" << endl;
}

void LatexGenerator::startConstraintParam()
{
   m_textStream << "\\item[{\\em ";
}

void LatexGenerator::endConstraintParam()
{
}

void LatexGenerator::startConstraintType()
{
   m_textStream << "} : {\\em ";
}

void LatexGenerator::endConstraintType()
{
   m_textStream << "}]";
}

void LatexGenerator::startConstraintDocs()
{
}

void LatexGenerator::endConstraintDocs()
{
}

void LatexGenerator::endConstraintList()
{
   m_textStream << "\\end{description}" << endl;
   m_textStream << "\\end{Desc}" << endl;
}

void LatexGenerator::escapeLabelName(const char *s)
{
   if (s == 0) {
      return;
   }

   const char *p = s;
   char c;

   QByteArray result; 

   // worst case allocation
   result.resize(qstrlen(s) + 1);

   int i;

   while ((c = *p++)) {
      switch (c) {
         case '|':
            m_textStream << "\\texttt{\"|}";
            break;
         case '!':
            m_textStream << "\"!";
            break;
         case '%':
            m_textStream << "\\%";
            break;
         case '{':
            m_textStream << "\\lcurly{}";
            break;
         case '}':
            m_textStream << "\\rcurly{}";
            break;
         case '~':
            m_textStream << "````~";
            break; // to get it a bit better in index together with other special characters
         // NOTE: adding a case here, means adding it to while below as well
         default:
            i = 0;
            // collect as long string as possible, before handing it to docify
            result[i++] = c;
            while ((c = *p) && c != '|' && c != '!' && c != '%' && c != '{' && c != '}' && c != '~') {
               result[i++] = c;
               p++;
            }
            result[i] = 0;
            docify(result);
            break;
      }
   }
}

void LatexGenerator::escapeMakeIndexChars(const char *s)
{
   if (s == 0) {
      return;
   }

   const char *p = s;
   char c;

   QByteArray result; 

   // worst case allocation
   result.resize(qstrlen(s) + 1);
   int i;

   while ((c = *p++)) {
      switch (c) {
         case '!':
            m_textStream << "\"!";
            break;
         case '"':
            m_textStream << "\"\"";
            break;
         case '@':
            m_textStream << "\"@";
            break;
         case '|':
            m_textStream << "\\texttt{\"|}";
            break;
         case '[':
            m_textStream << "[";
            break;
         case ']':
            m_textStream << "]";
            break;
         case '{':
            m_textStream << "\\lcurly{}";
            break;
         case '}':
            m_textStream << "\\rcurly{}";
            break;

         // NOTE: adding a case here, means adding it to while below as well
         default:
            i = 0;
            // collect as long string as possible, before handing it to docify
            result[i++] = c;

            while ((c = *p) && c != '"' && c != '@' && c != '[' && c != ']' && c != '!' && c != '{' && c != '}' && c != '|') {
               result[i++] = c;
               p++;
            }

            result[i] = 0;
            docify(result);
            break;
      }
   }
}

void LatexGenerator::startCodeFragment()
{
   m_textStream << "\n\\begin{DoxyCode}\n";
}

void LatexGenerator::endCodeFragment()
{
   m_textStream << "\\end{DoxyCode}\n";
}

void LatexGenerator::writeLineNumber(const char *ref, const QByteArray &fileName, const char *anchor, int len)
{
   static bool usePDFLatex   = Config::getBool("latex-pdf");
   static bool pdfHyperlinks = Config::getBool("latex-hyper-pdf");

   if (m_prettyCode) {
      QByteArray lineNumber;
      lineNumber = QString("%05d").arg(len).toUtf8();;

      if (! fileName.isEmpty() && ! sourceFileName.isEmpty()) {
         QByteArray lineAnchor;
         lineAnchor = QString("_l%05d").arg(len).toUtf8();

         lineAnchor.prepend(sourceFileName);
       
         if (usePDFLatex && pdfHyperlinks) {
            m_textStream << "\\hypertarget{" << stripPath(lineAnchor) << "}{}";
         }

         writeCodeLink(ref, fileName, anchor, lineNumber, 0);

      } else {
         codify(lineNumber);
      }

      m_textStream << " ";

   } else {
      m_textStream << len << " ";
   }
}

void LatexGenerator::startCodeLine(bool)
{
   col = 0;
}

void LatexGenerator::endCodeLine()
{
   codify("\n");
}

void LatexGenerator::startFontClass(const char *name)
{
   //if (!m_prettyCode) return;
   m_textStream << "\\textcolor{" << name << "}{";
}

void LatexGenerator::endFontClass()
{
   //if (!m_prettyCode) return;
   m_textStream << "}";
}

void LatexGenerator::startInlineHeader()
{
   if (Config_getBool("COMPACT_LATEX")) {
      m_textStream << "\\paragraph*{";
   } else {
      m_textStream << "\\subsubsection*{";
   }
}

void LatexGenerator::endInlineHeader()
{
   m_textStream << "}" << endl;
}

void LatexGenerator::lineBreak(const QByteArray &style)
{
   if (insideTabbing) {
      m_textStream << "\\\\\n";

   } else {
      m_textStream << "\\\\*\n";

   }
}

void LatexGenerator::startMemberDocSimple()
{
   m_textStream << "\\begin{DoxyFields}{";
   docify(theTranslator->trCompoundMembers());
   m_textStream << "}" << endl;
}

void LatexGenerator::endMemberDocSimple()
{
   m_textStream << "\\end{DoxyFields}" << endl;
}

void LatexGenerator::startInlineMemberType()
{
}

void LatexGenerator::endInlineMemberType()
{
   m_textStream << "&" << endl;
}

void LatexGenerator::startInlineMemberName()
{
}

void LatexGenerator::endInlineMemberName()
{
   m_textStream << "&" << endl;
}

void LatexGenerator::startInlineMemberDoc()
{
}

void LatexGenerator::endInlineMemberDoc()
{
   m_textStream << "\\\\\n\\hline\n" << endl;
}

void LatexGenerator::startLabels()
{
   m_textStream << "\\hspace{0.3cm}";
}

void LatexGenerator::writeLabel(const char *l, bool isLast)
{
   m_textStream << "{\\ttfamily [" << l << "]}";

   if (!isLast) {
      m_textStream << ", ";
   }
}

void LatexGenerator::endLabels()
{
}

