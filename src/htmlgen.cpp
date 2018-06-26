/*************************************************************************
 *
 * Copyright (C) 2014-2018 Barbara Geller & Ansel Sermersheim
 * Copyright (C) 1997-2014 by Dimitri van Heesch.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation under the terms of the GNU General Public License version 2
 * is hereby granted. No representations are made about the suitability of
 * this software for any purpose. It is provided "as is" without express or
 * implied warranty. See the GNU General Public License for more details.
 *
 * Documents produced by DoxyPress are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
*************************************************************************/

#include <QDir>
#include <QRegularExpression>

#include <stdlib.h>

#include <htmlgen.h>

#include <config.h>
#include <docparser.h>
#include <diagram.h>
#include <dot.h>
#include <doxy_build_info.h>
#include <doxy_globals.h>
#include <ftvhelp.h>
#include <htmldocvisitor.h>
#include <htmlhelp.h>
#include <image.h>
#include <layout.h>
#include <logos.h>
#include <language.h>
#include <message.h>
#include <resourcemgr.h>
#include <util.h>

//#define DBG_HTML(x) x;
#define DBG_HTML(x)

static QString g_header;
static QString g_footer;
static QString g_mathjax_code;

static void writeClientSearchBox(QTextStream &t, const QString &relPath)
{
   t << "        <div id=\"MSearchBox\" class=\"MSearchBoxInactive\">\n";
   t << "        <span class=\"left\">\n";
   t << "          <img id=\"MSearchSelect\" src=\"" << relPath << "search/mag_sel.png\"\n";
   t << "               onmouseover=\"return searchBox.OnSearchSelectShow()\"\n";
   t << "               onmouseout=\"return searchBox.OnSearchSelectHide()\"\n";
   t << "               alt=\"\"/>\n";
   t << "          <input type=\"text\" id=\"MSearchField\" value=\""
     << theTranslator->trSearch() << "\" accesskey=\"S\"\n";

   t << "               onfocus=\"searchBox.OnSearchFieldFocus(true)\" \n";
   t << "               onblur=\"searchBox.OnSearchFieldFocus(false)\" \n";
   t << "               onkeyup=\"searchBox.OnSearchFieldChange(event)\"/>\n";
   t << "          </span><span class=\"right\">\n";
   t << "            <a id=\"MSearchClose\" href=\"javascript:searchBox.CloseResultsWindow()\">"
     << "<img id=\"MSearchCloseImg\" border=\"0\" src=\"" << relPath << "search/close.png\" alt=\"\"/></a>\n";

   t << "          </span>\n";
   t << "        </div>\n";
}

static void writeServerSearchBox(QTextStream &t, const QString &relPath, bool highlightSearch)
{
   static bool externalSearch = Config::getBool("search-external");

   t << "        <div id=\"MSearchBox\" class=\"MSearchBoxInactive\">\n";
   t << "          <div class=\"left\">\n";
   t << "            <form id=\"FSearchBox\" action=\"" << relPath;

   if (externalSearch) {
      t << "search" << Doxy_Globals::htmlFileExtension;
   } else {
      t << "search.php";
   }

   t << "\" method=\"get\">\n";
   t << "              <img id=\"MSearchSelect\" src=\"" << relPath << "search/mag.png\" alt=\"\"/>\n";

   if (! highlightSearch) {
      t << "              <input type=\"text\" id=\"MSearchField\" name=\"query\" value=\""
        << theTranslator->trSearch() << "\" size=\"20\" accesskey=\"S\" \n";
      t << "                     onfocus=\"searchBox.OnSearchFieldFocus(true)\" \n";
      t << "                     onblur=\"searchBox.OnSearchFieldFocus(false)\"/>\n";
      t << "            </form>\n";
      t << "          </div><div class=\"right\"></div>\n";
      t << "        </div>\n";
   }
}

/// Clear a text block \a s from \a begin to \a end markers
QString clearBlock(const QString &output, const QString &begin, const QString &end)
{
   QString retval = output;

   while (true) {
      int beginPos = retval.indexOf(begin);
      int endPos   = retval.indexOf(end, beginPos);

      if (beginPos == -1 || endPos == -1 ) {
         break;
      }

      int len = (endPos + end.length()) - beginPos;
      retval.replace(beginPos, len, "");
   }

   return retval;
}

static QString selectBlock(const QString &s, const QString &name, bool enable)
{
   const QString begin   = "<!--BEGIN " + name + "-->";
   const QString end     = "<!--END " + name + "-->";

   const QString nobegin = "<!--BEGIN !" + name + "-->";
   const QString noend   = "<!--END !" + name + "-->";

   QString result = s;

   if (enable) {
      result = result.replace(begin, "");
      result = result.replace(end, "");
      result = clearBlock(result, nobegin, noend);

   } else {
      result = result.replace(nobegin, "");
      result = result.replace(noend, "");
      result = clearBlock(result, begin, end);
   }

   return result;
}

static QString getSearchBox(bool serverSide, const QString &relPath, bool highlightSearch)
{
   QString result;
   QTextStream t(&result);

   if (serverSide) {
      writeServerSearchBox(t, relPath, highlightSearch);
   } else {
      writeClientSearchBox(t, relPath);
   }

   return result;
}

static QString removeEmptyLines(const QString &s)
{
   QString retval;
   QString line;

   for (auto letter : s) {
      line.append(letter);

      if (letter == '\n')  {

         if (line.trimmed() != "") {
            retval.append(line);
         }

         line = "";
       }
   }

   if (line.trimmed() != "") {
      retval.append(line);
   }

   return retval;
}

static QString substituteHtmlKeywords(const QString &output, const QString &title,
                  const QString &relPath = QString(), const QString &navPath = QString())
{
   // Build CSS/Javascript tags depending on treeview, search engine settings

   QString generatedBy;
   QString treeViewCssJs;
   QString searchCssJs;
   QString searchBox;
   QString mathJaxJs;

   static const QDir configDir   = Config::getConfigDir();

   static QString projectName    = Config::getString("project-name");
   static QString projectVersion = Config::getString("project-version");
   static QString projectBrief   = Config::getString("project-brief");
   static QString projectLogo    = Config::getString("project-logo");

   static bool timeStamp         = Config::getBool("html-timestamp");
   static bool treeView          = Config::getBool("generate-treeview");
   static bool searchEngine      = Config::getBool("html-search");
   static bool serverBasedSearch = Config::getBool("search-server-based");

   static bool mathJax           = Config::getBool("use-mathjax");
   static QString mathJaxFormat  = Config::getEnum("mathjax-format");
   static bool disableIndex      = Config::getBool("disable-index");

   static bool hasProjectName    = ! projectName.isEmpty();
   static bool hasProjectVersion = ! projectVersion.isEmpty();
   static bool hasProjectBrief   = ! projectBrief.isEmpty();
   static bool hasProjectLogo    = ! projectLogo.isEmpty();

   static bool titleArea = (hasProjectName || hasProjectBrief || hasProjectLogo || (disableIndex && searchEngine));

   // always first
   QString cssFile = "doxypress.css";

   QString extraCssText = "";
   static const QStringList extraCssFile = Config::getList("html-stylesheets");

   for (auto fileName : extraCssFile) {

      if (! fileName.isEmpty()) {
         QFileInfo fi(configDir, fileName);

         if (fi.exists()) {
            extraCssText += "<link href=\"$relpath^" + stripPath(fileName) +
                  "\" rel=\"stylesheet\" type=\"text/css\"/>\n";
         } else {
            err("Unable to find stylesheet '%s'\n", csPrintable(fi.absoluteFilePath()));
         }
      }
   }

   if (timeStamp) {
      generatedBy = theTranslator->trGeneratedAt(dateToString(true), convertToHtml(projectName));

   } else {
      generatedBy = theTranslator->trGeneratedBy();
   }

   if (treeView) {
      treeViewCssJs = "<link href=\"$relpath^navtree.css\" rel=\"stylesheet\" type=\"text/css\"/>\n"
                      "<script type=\"text/javascript\" src=\"$relpath^resize.js\"></script>\n"
                      "<script type=\"text/javascript\" src=\"$relpath^navtreedata.js\"></script>\n"
                      "<script type=\"text/javascript\" src=\"$relpath^navtree.js\"></script>\n"
                      "<script type=\"text/javascript\">\n"
                      "  $(document).ready(initResizable);\n"
                      "  $(window).load(resizeHeight);\n"
                      "</script>";
   }

   if (searchEngine) {
      searchCssJs = "<link href=\"$relpath^search/search.css\" rel=\"stylesheet\" type=\"text/css\"/>\n";

      if (! serverBasedSearch) {
         searchCssJs += "<script type=\"text/javascript\" src=\"$relpath^search/searchdata.js\"></script>\n";
      }
      searchCssJs += "<script type=\"text/javascript\" src=\"$relpath^search/search.js\"></script>\n";

      if (! serverBasedSearch) {
         searchCssJs += "<script type=\"text/javascript\">\n"
                        "  $(document).ready(function() { init_search(); });\n"
                        "</script>";
      } else {
         searchCssJs += "<script type=\"text/javascript\">\n"
                        "  $(document).ready(function() {\n"
                        "    if ($('.searchresults').length > 0) { searchBox.DOMSearchField().focus(); }\n"
                        "  });\n"
                        "</script>\n";

         // OPENSEARCH_PROVIDER
         searchCssJs += "<link rel=\"search\" href=\"" + relPath +
                        "search_opensearch.php?v=opensearch.xml\" "
                        "type=\"application/opensearchdescription+xml\" title=\"" +
                        (hasProjectName ? projectName : "DoxyPress") + "\"/>";
      }
      searchBox = getSearchBox(serverBasedSearch, relPath, false);
   }

   if (mathJax) {
      QString path = Config::getString("mathjax-relpath");

      if (! path.isEmpty() && path.at(path.length() - 1) != '/') {
         path += "/";
      }

      if (path.isEmpty() || path.left(2) == "..") {
         // relative path
         path.prepend(relPath);
      }

      mathJaxJs = "<script type=\"text/x-mathjax-config\">\n"
                  "  MathJax.Hub.Config({\n"
                  "    extensions: [\"tex2jax.js\"";

      const QStringList mathJaxExtensions = Config::getList("mathjax-extensions");

      for (auto item : mathJaxExtensions) {
         mathJaxJs += ", \"" + item + ".js\"";
      }

      if (mathJaxFormat.isEmpty()) {
         mathJaxFormat = "HTML-CSS";
      }

      mathJaxJs += "],\n"
                   "    jax: [\"input/TeX\",\"output/" + mathJaxFormat + "\"],\n"
                   "});\n";

      if (! g_mathjax_code.isEmpty()) {
         mathJaxJs += g_mathjax_code;
         mathJaxJs += "\n";
      }

      mathJaxJs += "</script>";
      mathJaxJs += "<script type=\"text/javascript\" async src=\"" + path + "MathJax.js\"></script>\n";
   }

   QString result = output;

   // first substitute generic keywords
   if (! title.isEmpty()) {
      result = result.replace("$title", convertToHtml(title));
   }

   result = result.replace("$datetimeHHMM",   dateTimeHHMM());
   result = result.replace("$datetime",       dateToString(true));
   result = result.replace("$date",           dateToString(false));
   result = result.replace("$year",           yearToString());

   result = result.replace("$doxypressversion", versionString);
   result = result.replace("$doxygenversion",   versionString);      // compatibility

   result = result.replace("$projectname",    convertToHtml(projectName));
   result = result.replace("$projectversion", convertToHtml(projectVersion));
   result = result.replace("$projectbrief",   convertToHtml(projectBrief));
   result = result.replace("$projectlogo",    stripPath(projectLogo));

   // additional HTML only keywords
   result = result.replace("$navpath",         navPath);
   result = result.replace("$stylesheet",      cssFile);
   result = result.replace("$extrastylesheet", extraCssText);
   result = result.replace("$treeview",        treeViewCssJs);
   result = result.replace("$searchbox",       searchBox);
   result = result.replace("$search",          searchCssJs);
   result = result.replace("$mathjax",         mathJaxJs);
   result = result.replace("$generatedby",     generatedBy);

   result = result.replace("$relpath$", relPath); //<-- obsolete: for backwards compatibility only
   result = result.replace("$relpath^", relPath); //<-- must be last

   // additional HTML only conditional blocks
   result = selectBlock(result, "DISABLE_INDEX",     disableIndex);
   result = selectBlock(result, "GENERATE_TREEVIEW", treeView);
   result = selectBlock(result, "SEARCHENGINE",      searchEngine);
   result = selectBlock(result, "TITLEAREA",         titleArea);
   result = selectBlock(result, "PROJECT_NAME",      hasProjectName);
   result = selectBlock(result, "PROJECT_VERSION",   hasProjectVersion);
   result = selectBlock(result, "PROJECT_BRIEF",     hasProjectBrief);
   result = selectBlock(result, "PROJECT_LOGO",      hasProjectLogo);

   result = removeEmptyLines(result);

   return result;
}

HtmlCodeGenerator::HtmlCodeGenerator(QTextStream &t, const QString &relPath)
   : m_col(0), m_relPath(relPath), m_streamX(t)
{
}

void HtmlCodeGenerator::setRelativePath(const QString &path)
{
   m_relPath = path;
}

void HtmlCodeGenerator::codify(const QString &str)
{
   if (str.isEmpty()) {
      return;
   }

   static const int tabSize = Config::getInt("tab-size");

   int spacesToNextTabStop;
   bool isBackSlash = false;

   for (auto c : str) {

      if (isBackSlash) {
         isBackSlash = false;

         m_streamX << "\\";
         m_col++;
      }

      switch (c.unicode()) {

         case '\t':
            spacesToNextTabStop = tabSize - (m_col % tabSize);
            m_streamX << QString(spacesToNextTabStop, ' ');

            m_col += spacesToNextTabStop;
            break;

         case '\n':
            m_streamX << "\n";
            m_col = 0;
            break;

         case '\r':
            break;

         case '<':
            m_streamX << "&lt;";
            m_col++;
            break;

         case '>':
            m_streamX << "&gt;";
            m_col++;
            break;

         case '&':
            m_streamX << "&amp;";
            m_col++;
            break;

         case '"':
            m_streamX << "&quot;";
            m_col++;
            break;

         case '\'':
            m_streamX << "&#39;";
            m_col++;                // &apos; is not valid XHTML
            break;

         case '\\':
            isBackSlash = true;
            break;

         default:
            m_streamX << c;
            m_col++;
            break;
      }

   }
}

void HtmlCodeGenerator::docify(const QString &text)
{
   bool isBackSlash = false;

   for (auto c : text) {

      switch (c.unicode()) {
         case '<':
            m_streamX << "&lt;";
            break;

         case '>':
            m_streamX << "&gt;";
            break;

         case '&':
            m_streamX << "&amp;";
            break;

         case '"':
            m_streamX << "&quot;";
            break;

         case '\\':

            if (isBackSlash) {
              isBackSlash = false;

              m_streamX << "\\";

            } else {
              isBackSlash = true;

            }

            break;

         default:
            m_streamX << c;
      }

      if (isBackSlash && c != '\\') {
         isBackSlash = false;
      }
   }
}

void HtmlCodeGenerator::writeLineNumber(const QString &ref, const QString &filename, const QString &anchor, int len)
{
   QString lineNumber;
   QString lineAnchor;

   lineNumber = QString("%1").formatArg(len, 5, 10);
   lineAnchor = QString("l%1").formatArg(len, 5, 10,  QChar('0'));

   m_streamX << "<div class=\"line\">";
   m_streamX << "<a name=\"" << lineAnchor << "\"></a><span class=\"lineno\">";

   if (! filename.isEmpty()) {
      _writeCodeLink("line", ref, filename, anchor, lineNumber, 0);
   } else {
      codify(lineNumber);
   }

   m_streamX << "</span>";
   m_streamX << "&#160;";
}

void HtmlCodeGenerator::writeCodeLink(const QString &ref, const QString &f, const QString &anchor,
                  const QString &name, const QString &tooltip)
{
   _writeCodeLink("code", ref, f, anchor, name, tooltip);
}

void HtmlCodeGenerator::_writeCodeLink(const QString &className, const QString &ref, const QString &f,
                  const QString &anchor, const QString &name, const QString &tooltip)
{
   if (! ref.isEmpty()) {
      m_streamX << "<a class=\"" << className << "Ref\" ";
      m_streamX << externalLinkTarget() << externalRef(m_relPath, ref, false);

   } else {
      m_streamX << "<a class=\"" << className << "\" ";
   }

   m_streamX << "href=\"";
   m_streamX << externalRef(m_relPath, ref, true);

   if (! f.isEmpty()) {
      m_streamX << f << Doxy_Globals::htmlFileExtension;
   }

   if (! anchor.isEmpty()) {
      m_streamX << "#" << anchor;
   }

   m_streamX << "\"";

   if (! tooltip.isEmpty()) {
      m_streamX << " title=\"" << tooltip << "\"";
   }

   m_streamX << ">";
   docify(name);

   m_streamX << "</a>";
   m_col += name.length();
}

void HtmlCodeGenerator::writeTooltip(const QString &id, const DocLinkInfo &docInfo, const QString &decl,
                  const QString &desc, const SourceLinkInfo &defInfo, const SourceLinkInfo &declInfo)
{
   m_streamX << "<div class=\"ttc\" id=\"" << id << "\">";
   m_streamX << "<div class=\"ttname\">";

   if (! docInfo.url.isEmpty()) {
      m_streamX << "<a href=\"";
      m_streamX << externalRef(m_relPath, docInfo.ref, true);
      m_streamX << docInfo.url << Doxy_Globals::htmlFileExtension;

      if (! docInfo.anchor.isEmpty()) {
         m_streamX << "#" << docInfo.anchor;
      }

      m_streamX << "\">";
   }

   docify(docInfo.name);

   if (! docInfo.url.isEmpty()) {
      m_streamX << "</a>";
   }

   m_streamX << "</div>";

   if (! decl.isEmpty()) {
      m_streamX << "<div class=\"ttdeci\">";
      docify(decl);
      m_streamX << "</div>";
   }

   if (! desc.isEmpty()) {
      m_streamX << "<div class=\"ttdoc\">";

      // desc is already HTML escaped but there are still < and > signs
      docify(desc);

      m_streamX << "</div>";
   }

   if (! defInfo.file.isEmpty()) {
      m_streamX << "<div class=\"ttdef\"><b>Definition:</b> ";

      if (! defInfo.url.isEmpty()) {
         m_streamX << "<a href=\"";
         m_streamX << externalRef(m_relPath, defInfo.ref, true);
         m_streamX << defInfo.url << Doxy_Globals::htmlFileExtension;
         if (!defInfo.anchor.isEmpty()) {
            m_streamX << "#" << defInfo.anchor;
         }
         m_streamX << "\">";
      }

      m_streamX << defInfo.file << ":" << defInfo.line;
      if (!defInfo.url.isEmpty()) {
         m_streamX << "</a>";
      }
      m_streamX << "</div>";
   }

   if (! declInfo.file.isEmpty()) {
      m_streamX << "<div class=\"ttdecl\"><b>Declaration:</b> ";
      if (!declInfo.url.isEmpty()) {
         m_streamX << "<a href=\"";
         m_streamX << externalRef(m_relPath, declInfo.ref, true);
         m_streamX << declInfo.url << Doxy_Globals::htmlFileExtension;
         if (!declInfo.anchor.isEmpty()) {
            m_streamX << "#" << declInfo.anchor;
         }
         m_streamX << "\">";
      }
      m_streamX << declInfo.file << ":" << declInfo.line;
      if (!declInfo.url.isEmpty()) {
         m_streamX << "</a>";
      }
      m_streamX << "</div>";
   }

   m_streamX << "</div>" << endl;
}


void HtmlCodeGenerator::startCodeLine(bool hasLineNumbers)
{
   if (! hasLineNumbers) {
      m_streamX << "<div class=\"line\">";
   }

   m_col = 0;
}

void HtmlCodeGenerator::endCodeLine()
{
   m_streamX << "</div>";
}

void HtmlCodeGenerator::startFontClass(const QString &s)
{
   m_streamX << "<span class=\"" << s << "\">";
}

void HtmlCodeGenerator::endFontClass()
{
   m_streamX << "</span>";
}

void HtmlCodeGenerator::writeCodeAnchor(const QString &anchor)
{
   m_streamX << "<a name=\"" << anchor << "\"></a>";
}

HtmlGenerator::HtmlGenerator() : OutputGenerator()
{
   m_dir = Config::getString("html-output");
   m_emptySection = false;
}

HtmlGenerator::~HtmlGenerator()
{
}

void HtmlGenerator::init()
{
   static const QString htmlDirName = Config::getString("html-output");
   static const QString htmlHeader  = Config::getString("html-header");
   static const QString htmlFooter  = Config::getString("html-footer");

   static bool useMathJax  = Config::getBool("use-mathjax");

   QDir d(htmlDirName);

   if (! d.exists() && ! d.mkdir(htmlDirName)) {
      err("HTML Generator, unable to create output directory %s\n", csPrintable(htmlDirName));
      Doxy_Work::stopDoxyPress();
   }

   if (! htmlHeader.isEmpty()) {
      g_header = fileToString(htmlHeader);
   } else {
      QByteArray data = ResourceMgr::instance().getAsString("html/header.html");
      g_header = QString::fromUtf8(data);
   }

   if (! htmlFooter.isEmpty()) {
      g_footer = fileToString(htmlFooter);
   } else {
      QByteArray data = ResourceMgr::instance().getAsString("html/footer.html");
      g_footer = QString::fromUtf8(data);
   }

   if (useMathJax) {
      static const QString mathJaxCodeFile = Config::getString("mathjax-codefile");

      if (! mathJaxCodeFile.isEmpty()) {
         g_mathjax_code = fileToString(mathJaxCodeFile);
      }
   }
   createSubDirs(d);

   ResourceMgr &mgr = ResourceMgr::instance();
   mgr.copyResourceAs("html/tabs.css",  htmlDirName, "tabs.css");
   mgr.copyResourceAs("html/jquery.js", htmlDirName, "jquery.js");

   if (Config::getBool("interactive-svg")) {
      mgr.copyResourceAs("html/svgpan.js", htmlDirName, "svgpan.js");
   }

   QString fileName = htmlDirName + "/dynsections.js";
   QFile f(fileName);

   if (f.open(QIODevice::WriteOnly)) {
      QByteArray resource = mgr.getAsString("html/dynsections.js");

      if (! resource.isEmpty()) {
         QTextStream t(&f);
         t << resource;

         if (Config::getBool("source-code") && Config::getBool("source-tooltips")) {
            t << endl <<
              "$(document).ready(function() {\n"
              "  $('.code,.codeRef').each(function() {\n"
              "    $(this).data('powertip',$('#'+$(this).attr('href').replace(/.*\\//,'').replace(/[^a-z_A-Z0-9]/g,'_')).html());\n"
              "    $(this).powerTip({ placement: 's', smartPlacement: true, mouseOnToPopup: true });\n"
              "  });\n"
              "});\n";
         }

      } else {
         err("Unable to find or load the resource file dynsections.js");

      }

   } else {
      err("Unable to open file for writing %s, error: %d\n", csPrintable(fileName), f.error());

   }

}

/// Additional initialization after indices have been created
void HtmlGenerator::writeTabData()
{
   Doxy_Globals::indexList.addStyleSheetFile("tabs.css");
   static const QString htmlDirName = Config::getString("html-output");

   ResourceMgr &mgr = ResourceMgr::instance();

   // writeColoredImgData(htmlDirName,colored_tab_data);

   mgr.copyResourceAs("html/tab_a.lum",      htmlDirName, "tab_a.png");
   mgr.copyResourceAs("html/tab_b.lum",      htmlDirName, "tab_b.png");
   mgr.copyResourceAs("html/tab_h.lum",      htmlDirName, "tab_h.png");
   mgr.copyResourceAs("html/tab_s.lum",      htmlDirName, "tab_s.png");
   mgr.copyResourceAs("html/nav_h.lum",      htmlDirName, "nav_h.png");
   mgr.copyResourceAs("html/nav_f.lum",      htmlDirName, "nav_f.png");
   mgr.copyResourceAs("html/bc_s.luma",      htmlDirName, "bc_s.png");
   mgr.copyResourceAs("html/doxypress.luma", htmlDirName, "doxypress.png");
   mgr.copyResourceAs("html/closed.luma",    htmlDirName, "closed.png");
   mgr.copyResourceAs("html/open.luma",      htmlDirName, "open.png");
   mgr.copyResourceAs("html/bdwn.luma",      htmlDirName, "bdwn.png");
   mgr.copyResourceAs("html/sync_on.luma",   htmlDirName, "sync_on.png");
   mgr.copyResourceAs("html/sync_off.luma",  htmlDirName, "sync_off.png");
   mgr.copyResourceAs("html/nav_g.png",      htmlDirName, "nav_g.png");
}

void HtmlGenerator::writeSearchData(const QString &dir)
{
   static const bool serverBasedSearch = Config::getBool("search-server-based");

   ResourceMgr &mgr = ResourceMgr::instance();

   mgr.copyResourceAs("html/search_l.png", dir, "search_l.png");
   Doxy_Globals::indexList.addImageFile("search/search_l.png");

   mgr.copyResourceAs("html/search_m.png", dir, "search_m.png");
   Doxy_Globals::indexList.addImageFile("search/search_m.png");

   mgr.copyResourceAs("html/search_r.png", dir, "search_r.png");
   Doxy_Globals::indexList.addImageFile("search/search_r.png");

   if (serverBasedSearch) {
      mgr.copyResourceAs("html/mag.png", dir, "mag.png");
      Doxy_Globals::indexList.addImageFile("search/mag.png");

   } else {
      mgr.copyResourceAs("html/close.png", dir, "close.png");
      Doxy_Globals::indexList.addImageFile("search/close.png");

      mgr.copyResourceAs("html/mag_sel.png", dir, "mag_sel.png");
      Doxy_Globals::indexList.addImageFile("search/mag_sel.png");
   }

   QString outputName = Config::getString("html-output") + "/search";

   QString fileName = outputName + "/search.css";
   QFile f(fileName);

   if (f.open(QIODevice::WriteOnly)) {
      static bool disableIndex = Config::getBool("disable-index");

      QByteArray data;

      if (disableIndex) {
         data = mgr.getAsString("html/search_nomenu.css");
      } else {
         data = mgr.getAsString("html/search.css");
      }

      QString searchCss = QString::fromUtf8(data);

      if (! searchCss.isEmpty()) {
         QTextStream t(&f);

         searchCss = replaceColorMarkers(searchCss);

         searchCss.replace("$doxypressversion", versionString);
         searchCss.replace("$doxygenversion",   versionString);         // compatibility

         t << searchCss;
         Doxy_Globals::indexList.addStyleSheetFile("search/search.css");
      }

   } else {
      err("Unable to open file for writing %s, error: %d\n", csPrintable(fileName), f.error());
   }
}

void HtmlGenerator::writeStyleSheetFile(QFile &file)
{
   QTextStream t(&file);
   QString resData = ResourceMgr::instance().getAsString("html/doxypress.css");

   if (resData.isEmpty()) {
      fprintf(stderr, "\n\nIssue loading the default stylesheet file.\nPlease submit a bug report to "
              "the developers at info@copperspice.com\n");

   } else {
      resData.replace("$doxypressversion", versionString);
      resData.replace("$doxygenversion",   versionString);         // compatibility

      t << replaceColorMarkers(resData);
   }
}

void HtmlGenerator::writeHeaderFile(QFile &file)
{
   QTextStream t(&file);

   t << "<!-- HTML header for DoxyPress " << versionString << "-->" << endl;
   t << ResourceMgr::instance().getAsString("html/header.html");
}

void HtmlGenerator::writeFooterFile(QFile &file)
{
   QTextStream t(&file);

   t << "<!-- HTML footer for DoxyPress " << versionString << "-->" <<  endl;
   t << ResourceMgr::instance().getAsString("html/footer.html");
}

void HtmlGenerator::startFile(const QString &name, const QString &, const QString &title)
{
   QString fileName = name;

   m_lastTitle    = title;
   m_relativePath = relativePathToRoot(fileName);

   if (fileName.right(Doxy_Globals::htmlFileExtension.length()) != Doxy_Globals::htmlFileExtension) {
      fileName += Doxy_Globals::htmlFileExtension;
   }

   startPlainFile(fileName);
   m_codeGen = QMakeShared<HtmlCodeGenerator> (m_textStream, m_relativePath);

   //
   Doxy_Globals::indexList.addIndexFile(fileName);

   m_lastFile = fileName;
   m_textStream << substituteHtmlKeywords(g_header, filterTitle(title), m_relativePath);
   m_textStream << "<!-- " << theTranslator->trGeneratedBy() << " DoxyPress " << versionString << " -->" << endl;

   static bool searchEngine = Config::getBool("html-search");

   if (searchEngine /*&& !generateTreeView*/) {
      m_textStream << "<script type=\"text/javascript\">\n";

      m_textStream << "var searchBox = new SearchBox(\"searchBox\", \""
        << m_relativePath << "search\",false,'" << theTranslator->trSearch() << "');\n";

      m_textStream << "</script>\n";
   }

   m_sectionCount = 0;
}

void HtmlGenerator::writeSearchInfo(QTextStream &t_stream, const QString &relPath)
{
   static bool searchEngine      = Config::getBool("html-search");
   static bool serverBasedSearch = Config::getBool("search-server-based");

   if (searchEngine && !serverBasedSearch) {
      (void)relPath;

      t_stream << "<!-- window showing the filter options -->\n";
      t_stream << "<div id=\"MSearchSelectWindow\"\n";
      t_stream << "     onmouseover=\"return searchBox.OnSearchSelectShow()\"\n";
      t_stream << "     onmouseout=\"return searchBox.OnSearchSelectHide()\"\n";
      t_stream << "     onkeydown=\"return searchBox.OnSearchSelectKey(event)\">\n";
      t_stream << "</div>\n";
      t_stream << "\n";
      t_stream << "<!-- iframe showing the search results (closed by default) -->\n";
      t_stream << "<div id=\"MSearchResultsWindow\">\n";
      t_stream << "<iframe src=\"javascript:void(0)\" frameborder=\"0\" \n";
      t_stream << "        name=\"MSearchResults\" id=\"MSearchResults\">\n";
      t_stream << "</iframe>\n";
      t_stream << "</div>\n";
      t_stream << "\n";
   }
}

void HtmlGenerator::writeSearchInfo()
{
   writeSearchInfo(m_textStream, m_relativePath);
}

QString HtmlGenerator::writeLogoAsString(const QString &path)
{
   static bool timeStamp = Config::getBool("html-timestamp");
   QString result;

   if (timeStamp) {
      result += theTranslator->trGeneratedAt(dateToString(true), Config::getString("project-name"));

   } else {
      result += theTranslator->trGeneratedBy();
   }

   result += "&#160;\n<a href=\"http://www.copperspice.com/documentation-doxypress.html\">";
   result += "<img class=\"footer\" src=\"";
   result += path;
   result += "doxypress.png\" alt=\"DoxyPress\"/></a>";
   result += versionString;
   result += " ";

   return result;
}

void HtmlGenerator::writeLogo()
{
   m_textStream << writeLogoAsString(m_relativePath);
}

void HtmlGenerator::writePageFooter(QTextStream &t_stream, const QString &lastTitle, const QString &relPath, const QString &navPath)
{
   t_stream << substituteHtmlKeywords(g_footer, lastTitle, relPath, navPath);
}

void HtmlGenerator::writeFooter(const QString &navPath)
{
   writePageFooter(m_textStream, m_lastTitle, m_relativePath, navPath);
}

void HtmlGenerator::endFile()
{
   endPlainFile();
}

void HtmlGenerator::startProjectNumber()
{
   m_textStream << "<h3 class=\"version\">";
}

void HtmlGenerator::endProjectNumber()
{
   m_textStream << "</h3>";
}

void HtmlGenerator::writeStyleInfo(int part)
{
   if (part == 0) {
      // write default style sheet

      startPlainFile("doxypress.css");
      QString resData = ResourceMgr::instance().getAsString("html/doxypress.css");

      if (resData.isEmpty()) {
         fprintf(stderr, "\n\nIssue loading the default stylesheet file.\nPlease submit a bug report to "
               "the developers at info@copperspice.com\n");

      } else {
         resData.replace("$doxypressversion", versionString);
         resData.replace("$doxygenversion",   versionString);            // compatibility

         m_textStream << replaceColorMarkers(resData);
      }

      endPlainFile();
      Doxy_Globals::indexList.addStyleSheetFile("doxypress.css");

      // part two
      static const QDir configDir           = Config::getConfigDir();
      static const QStringList extraCssFile = Config::getList("html-stylesheets");

      for (auto fileName : extraCssFile) {

         if (! fileName.isEmpty()) {
            QFileInfo fi(configDir, fileName);

            if (fi.exists()) {
               Doxy_Globals::indexList.addStyleSheetFile(fi.fileName());
            } else {
               err("Unable to find stylesheet '%s'\n", csPrintable(fi.absoluteFilePath()));
            }
         }
      }

      Doxy_Globals::indexList.addStyleSheetFile("jquery.js");
      Doxy_Globals::indexList.addStyleSheetFile("dynsections.js");

      if (Config::getBool("interactive-svg"))   {
         Doxy_Globals::indexList.addStyleSheetFile("svgpan.js");
      }
   }
}

void HtmlGenerator::startDoxyAnchor(const QString &, const QString &, const QString &anchor, const QString &, const QString &)
{
   m_textStream << "<a class=\"anchor\" id=\"" << anchor << "\"></a>";
}

void HtmlGenerator::endDoxyAnchor(const QString &, const QString &)
{
}

// void HtmlGenerator::newParagraph()
// {
//    m_textStream << endl << "<p>" << endl;
// }

void HtmlGenerator::startParagraph(const QString &className)
{
   if (className.isEmpty()) {
      m_textStream << endl << "<p>";

   } else {
      m_textStream << endl << "<p class=\"" << className << "\">";
   }
}

void HtmlGenerator::endParagraph()
{
   m_textStream << "</p>" << endl;
}

void HtmlGenerator::writeString(const QString &text)
{
   m_textStream << text;
}

void HtmlGenerator::startIndexListItem()
{
   m_textStream << "<li>";
}

void HtmlGenerator::endIndexListItem()
{
   m_textStream << "</li>" << endl;
}

void HtmlGenerator::startIndexItem(const QString &ref, const QString &f)
{
   if (! ref.isEmpty() || ! f.isEmpty()) {

      if (! ref.isEmpty()) {
         m_textStream << "<a class=\"elRef\" ";
         m_textStream << externalLinkTarget() << externalRef(m_relativePath, ref, false);

      } else {
         m_textStream << "<a class=\"el\" ";
      }

      m_textStream << "href=\"";
      m_textStream << externalRef(m_relativePath, ref, true);

      if (! f.isEmpty()) {
         m_textStream << f << Doxy_Globals::htmlFileExtension << "\">";
      }

   } else {
      m_textStream << "<b>";
   }
}

void HtmlGenerator::endIndexItem(const QString &ref, const QString &f)
{
   if (! ref.isEmpty() || ! f.isEmpty()) {
      m_textStream << "</a>";

   } else {
      m_textStream << "</b>";
   }
}

void HtmlGenerator::writeStartAnnoItem(const QString &, const QString &f, const QString &path, const QString &name)
{
   m_textStream << "<li>";

   if (! path.isEmpty()) {
      docify(path);
   }

   m_textStream << "<a class=\"el\" href=\"" << f << Doxy_Globals::htmlFileExtension << "\">";
   docify(name);

   m_textStream << "</a> ";
}

void HtmlGenerator::writeObjectLink(const QString &ref, const QString &f, const QString &anchor, const QString &name)
{
   if (! ref.isEmpty()) {
      m_textStream << "<a class=\"elRef\" ";
      m_textStream << externalLinkTarget() << externalRef(m_relativePath, ref, false);

   } else {
      m_textStream << "<a class=\"el\" ";
   }

   m_textStream << "href=\"";
   m_textStream << externalRef(m_relativePath, ref, true);

   if (! f.isEmpty()) {
      m_textStream << f << Doxy_Globals::htmlFileExtension;
   }

   if (! anchor.isEmpty()) {
      m_textStream << "#" << anchor;
   }

   m_textStream << "\">";
   docify(name);

   m_textStream << "</a>";
}

void HtmlGenerator::startTextLink(const QString &f, const QString &anchor)
{
   m_textStream << "<a href=\"";

   if (! f.isEmpty()) {
      m_textStream << m_relativePath << f << Doxy_Globals::htmlFileExtension;
   }

   if (! anchor.isEmpty()) {
      m_textStream << "#" << anchor;
   }

   m_textStream << "\">";
}

void HtmlGenerator::endTextLink()
{
   m_textStream << "</a>";
}

void HtmlGenerator::startHtmlLink(const QString &url)
{
   static bool generateTreeView = Config::getBool("generate-treeview");
   m_textStream << "<a ";

   if (generateTreeView) {
      m_textStream << "target=\"top\" ";
   }

   m_textStream << "href=\"";

   if (! url.isEmpty()) {
      m_textStream << url;
   }

   m_textStream << "\">";
}

void HtmlGenerator::endHtmlLink()
{
   m_textStream << "</a>";
}

void HtmlGenerator::startGroupHeader(int extraIndentLevel)
{
   if (extraIndentLevel == 2) {
      m_textStream << "<h4 class=\"groupheader\">";

   } else if (extraIndentLevel == 1) {
      m_textStream << "<h3 class=\"groupheader\">";

   } else { // extraIndentLevel==0
      m_textStream << "<h2 class=\"groupheader\">";
   }
}

void HtmlGenerator::endGroupHeader(int extraIndentLevel)
{
   if (extraIndentLevel == 2) {
      m_textStream << "</h4>" << endl;

   } else if (extraIndentLevel == 1) {
      m_textStream << "</h3>" << endl;

   } else {
      m_textStream << "</h2>" << endl;
   }
}

void HtmlGenerator::startSection(const QString &lab, const QString &, SectionInfo::SectionType type)
{
   switch (type) {
      case SectionInfo::Page:
         m_textStream << "\n\n<h1>";
         break;
      case SectionInfo::Section:
         m_textStream << "\n\n<h2>";
         break;
      case SectionInfo::Subsection:
         m_textStream << "\n\n<h3>";
         break;
      case SectionInfo::Subsubsection:
         m_textStream << "\n\n<h4>";
         break;
      case SectionInfo::Paragraph:
         m_textStream << "\n\n<h5>";
         break;
      default:
         assert(0);
         break;
   }

   m_textStream << "<a class=\"anchor\" id=\"" << lab << "\"></a>";
}

void HtmlGenerator::endSection(const QString &, SectionInfo::SectionType type)
{
   switch (type) {
      case SectionInfo::Page:
         m_textStream << "</h1>";
         break;
      case SectionInfo::Section:
         m_textStream << "</h2>";
         break;
      case SectionInfo::Subsection:
         m_textStream << "</h3>";
         break;
      case SectionInfo::Subsubsection:
         m_textStream << "</h4>";
         break;
      case SectionInfo::Paragraph:
         m_textStream << "</h5>";
         break;
      default:
         assert(0);
         break;
   }
}

void HtmlGenerator::docify(const QString &text)
{
   docify(text, false);
}

void HtmlGenerator::docify(const QString &text, bool inHtmlComment)
{
   bool isBackSlash = false;

   for (auto c : text) {

      switch (c.unicode()) {
         case '<':
            m_textStream << "&lt;";
            break;

         case '>':
            m_textStream << "&gt;";
            break;

         case '&':
            m_textStream << "&amp;";
            break;

         case '"':
            m_textStream << "&quot;";
            break;

         case '-':
            if (inHtmlComment) {
               m_textStream << "&#45;";
            } else {
               m_textStream << "-";
            }
            break;

         case '\\':
            if (isBackSlash) {
              isBackSlash = false;

              m_textStream << "\\\\";

            } else {
              isBackSlash = true;

            }
            break;

         default:
            m_textStream << c;
      }

      if (isBackSlash && c != '\\') {
         isBackSlash = false;
      }

   }
}

void HtmlGenerator::writeChar(char c)
{
   char cs[2];
   cs[0] = c;
   cs[1] = 0;

   docify(cs);
}

static void startSectionHeader(QTextStream &t_stream, const QString &relPath, int sectionCount)
{
   static bool dynamicSections = Config::getBool("html-dynamic-sections");

   if (dynamicSections) {
      t_stream << "<div id=\"dynsection-" << sectionCount << "\" "
        "onclick=\"return toggleVisibility(this)\" "
        "class=\"dynheader closed\" "
        "style=\"cursor:pointer;\">" << endl;

      t_stream << "  <img id=\"dynsection-" << sectionCount << "-trigger\" src=\""
        << relPath << "closed.png\" alt=\"+\"/> ";

   } else {
      t_stream << "<div class=\"dynheader\">" << endl;

   }
}

static void endSectionHeader(QTextStream &t_stream)
{
   // m_stream << "<!-- endSectionHeader -->";

   t_stream << "</div>" << endl;
}

static void startSectionSummary(QTextStream &t_stream, int sectionCount)
{
   //t << "<!-- startSectionSummary -->";
   static bool dynamicSections = Config::getBool("html-dynamic-sections");

   if (dynamicSections) {
      t_stream << "<div id=\"dynsection-" << sectionCount << "-summary\" "
         "class=\"dynsummary\" "
         "style=\"display:block;\">" << endl;
   }
}

static void endSectionSummary(QTextStream &t_stream)
{
   //t << "<!-- endSectionSummary -->";
   static bool dynamicSections = Config::getBool("html-dynamic-sections");

   if (dynamicSections) {
      t_stream << "</div>" << endl;
   }
}

static void startSectionContent(QTextStream &t_stream, int sectionCount)
{
   //t << "<!-- startSectionContent -->";
   static bool dynamicSections = Config::getBool("html-dynamic-sections");

   if (dynamicSections) {
      t_stream << "<div id=\"dynsection-" << sectionCount << "-content\" "
        "class=\"dyncontent\" "
        "style=\"display:none;\">" << endl;

   } else {
      t_stream << "<div class=\"dyncontent\">" << endl;
   }
}

static void endSectionContent(QTextStream &t_stream)
{
   //t << "<!-- endSectionContent -->";
   t_stream << "</div>" << endl;
}

void HtmlGenerator::startClassDiagram()
{
   startSectionHeader(m_textStream, m_relativePath, m_sectionCount);
}

void HtmlGenerator::endClassDiagram(const ClassDiagram &d, const QString &fname, const QString &name)
{
   endSectionHeader(m_textStream);
   startSectionSummary(m_textStream, m_sectionCount);

   endSectionSummary(m_textStream);
   startSectionContent(m_textStream, m_sectionCount);

   m_textStream << " <div class=\"center\">" << endl;
   m_textStream << "  <img src=\"";
   m_textStream << m_relativePath << fname << ".png\" usemap=\"#";
   docify(name);

   m_textStream << "_map\" alt=\"\"/>" << endl;
   m_textStream << "  <map id=\"";
   docify(name);

   m_textStream << "_map\" name=\"";
   docify(name);

   m_textStream << "_map\">" << endl;
   d.writeImage(m_textStream, m_dir, m_relativePath, fname);

   m_textStream << " </div>";
   endSectionContent(m_textStream);

   m_sectionCount++;
}

void HtmlGenerator::startMemberList()
{
   DBG_HTML(m_textStream << "<!-- startMemberList -->" << endl)
}

void HtmlGenerator::endMemberList()
{
   DBG_HTML(m_textStream << "<!-- endMemberList -->" << endl)
}

// anonymous type:
//  0 = single column right aligned
//  1 = double column left aligned
//  2 = single column left aligned
void HtmlGenerator::startMemberItem(const QString &anchor, int annoType, const QString &inheritId, bool deprecated)
{
   if (m_emptySection) {
      m_textStream << "<table class=\"memberdecls\">" << endl;
      m_emptySection = false;
   }

   if (deprecated) {
      m_textStream << "<tr class=\"deprecated memitem:" << anchor;
   } else {
      m_textStream << "<tr class=\"memitem:" << anchor;
   }

   if (! inheritId.isEmpty()) {
      m_textStream << " inherit " << inheritId;
   }

   m_textStream << "\">";

   switch (annoType) {
      case 0:
         m_textStream << "<td class=\"memItemLeft\" align=\"right\" valign=\"top\">";
         break;
      case 1:
         m_textStream << "<td class=\"memItemLeft\" >";
         break;
      case 2:
         m_textStream << "<td class=\"memItemLeft\" valign=\"top\">";
         break;
      default:
         m_textStream << "<td class=\"memTemplParams\" colspan=\"2\">";
         break;
   }
}

void HtmlGenerator::endMemberItem()
{
   m_textStream << "</td></tr>";
   m_textStream << endl;
}

void HtmlGenerator::startMemberTemplateParams()
{
}

void HtmlGenerator::endMemberTemplateParams(const QString &anchor, const QString &inheritId)
{
   m_textStream << "</td></tr>" << endl;
   m_textStream << "<tr class=\"memitem:" << anchor;

   if (! inheritId.isEmpty()) {
      m_textStream << " inherit " << inheritId;
   }

   m_textStream << "\"><td class=\"memTemplItemLeft\" align=\"right\" valign=\"top\">";
}


void HtmlGenerator::insertMemberAlign(bool templ)
{
   QString className = templ ? QString("memTemplItemRight") : QString("memItemRight");
   m_textStream << "&#160;</td><td class=\"" << className << "\" valign=\"bottom\">";
}

void HtmlGenerator::startMemberDescription(const QString &anchor, const QString &inheritId)
{
   if (m_emptySection) {
      m_textStream << "<table class=\"memberdecls\">" << endl;
      m_emptySection = false;
   }

   m_textStream << "<tr class=\"memdesc:" << anchor;

   if (! inheritId.isEmpty()) {
      m_textStream << " inherit " << inheritId;
   }

   m_textStream << "\"><td class=\"mdescLeft\">&#160;</td><td class=\"mdescRight\">";
}

void HtmlGenerator::endMemberDescription()
{
   DBG_HTML(m_textStream << "<!-- endMemberDescription -->" << endl)
   m_textStream << "<br /></td></tr>" << endl;
}

void HtmlGenerator::startMemberSections()
{
   DBG_HTML(m_textStream << "<!-- startMemberSections -->" << endl)
   m_emptySection = true;

   // we postpone writing <table> until we actually
   // write a row to prevent empty tables, which
   // are not valid XHTML!
}

void HtmlGenerator::endMemberSections()
{
   DBG_HTML(m_textStream << "<!-- endMemberSections -->" << endl)

   if (! m_emptySection) {
      m_textStream << "</table>" << endl;
   }
}

void HtmlGenerator::startMemberHeader(const QString &anchor)
{
   DBG_HTML(m_textStream << "<!-- startMemberHeader -->" << endl)

   if (! m_emptySection) {
      m_textStream << "</table>";
      m_emptySection = true;
   }

   if (m_emptySection) {
      m_textStream << "<table class=\"memberdecls\">" << endl;
      m_emptySection = false;
   }

   m_textStream << "<tr class=\"heading\"><td colspan=\"2\"><h2 class=\"groupheader\">";

   if (! anchor.isEmpty()) {
      m_textStream << "<a name=\"" << anchor << "\"></a>" << endl;
   }
}

void HtmlGenerator::endMemberHeader()
{
   DBG_HTML(m_textStream << "<!-- endMemberHeader -->" << endl)
   m_textStream << "</h2></td></tr>" << endl;
}

void HtmlGenerator::startMemberSubtitle()
{
   DBG_HTML( << "<!-- startMemberSubtitle -->" << endl)
   m_textStream << "<tr><td class=\"ititle\" colspan=\"2\">";
}

void HtmlGenerator::endMemberSubtitle()
{
   DBG_HTML(m_textStream << "<!-- endMemberSubtitle -->" << endl)
   m_textStream << "</td></tr>" << endl;
}

void HtmlGenerator::startIndexList()
{
   m_textStream << "<table>"  << endl;
}

void HtmlGenerator::endIndexList()
{
   m_textStream << "</table>" << endl;
}

void HtmlGenerator::startIndexKey()
{
   // inserted 'class = ...', 02 jan 2002, jh
   m_textStream << "  <tr><td class=\"indexkey\">";
}

void HtmlGenerator::endIndexKey()
{
   m_textStream << "</td>";
}

void HtmlGenerator::startIndexValue(bool)
{
   // inserted 'class = ...', 02 jan 2002, jh
   m_textStream << "<td class=\"indexvalue\">";
}

void HtmlGenerator::endIndexValue(const QString &, bool)
{
   m_textStream << "</td></tr>" << endl;
}

void HtmlGenerator::startMemberDocList()
{
   DBG_HTML(m_textStream << "<!-- startMemberDocList -->" << endl;)
}

void HtmlGenerator::endMemberDocList()
{
   DBG_HTML(m_textStream << "<!-- endMemberDocList -->" << endl;)
}

void HtmlGenerator::startMemberDoc(const QString &clName, const QString &memName,
                  const QString &anchor, const QString &title, bool showInline)
{
   DBG_HTML(m_textStream << "<!-- startMemberDoc -->" << endl;)

/* odd styple where a tab sticks up and only the method name is displayed
   set up as an tag option to turn on/off

   m_textStream << "\n<h2 class=\"memtitle\">" << title << " "
                << "<a href=\"#" << anchor << "\" class=\"permalink\"" "title=\"Permalink to this headline\">&#9854;</a>"
                << "</h2>" << endl;
*/

   m_textStream << "\n<div class=\"memitem\">" << endl;
   m_textStream << "<div class=\"memproto\">" << endl;
}

void HtmlGenerator::startMemberDocPrefixItem()
{
   DBG_HTML(m_textStream << "<!-- startMemberDocPrefixItem -->" << endl;)
   m_textStream << "<div class=\"memtemplate\">" << endl;
}

void HtmlGenerator::endMemberDocPrefixItem()
{
   DBG_HTML(m_textStream << "<!-- endMemberDocPrefixItem -->" << endl;)
   m_textStream << "</div>" << endl;
}

void HtmlGenerator::startMemberDocName(bool /*align*/)
{
   DBG_HTML(m_textStream << "<!-- startMemberDocName -->" << endl;)

   m_textStream << "      <table class=\"memname\">" << endl;

   m_textStream << "        <tr>" << endl;
   m_textStream << "          <td class=\"memname\">";
}

void HtmlGenerator::endMemberDocName()
{
   DBG_HTML(m_textStream << "<!-- endMemberDocName -->" << endl;)
   m_textStream << "</td>" << endl;
}

void HtmlGenerator::startParameterList(bool openBracket)
{
   DBG_HTML(m_textStream << "<!-- startParameterList -->" << endl;)

   m_textStream << "          <td>";

   if (openBracket) {
      m_textStream << "(";
   }

   m_textStream << "</td>" << endl;
}

void HtmlGenerator::startParameterType(bool first, const QString &key)
{
   if (first) {
      DBG_HTML(m_textStream << "<!-- startFirstParameterType -->" << endl;)
      m_textStream << "          <td class=\"paramtype\">";

   } else {
      DBG_HTML(m_textStream << "<!-- startParameterType -->" << endl;)
      m_textStream << "        <tr>" << endl;
      m_textStream << "          <td class=\"paramkey\">";

      if (! key.isEmpty()) {
         m_textStream << key;
      }

      m_textStream << "</td>" << endl;
      m_textStream << "          <td></td>" << endl;
      m_textStream << "          <td class=\"paramtype\">";
   }
}

void HtmlGenerator::endParameterType()
{
   DBG_HTML(m_textStream << "<!-- endParameterType -->" << endl;)
   m_textStream << "&#160;</td>" << endl;
}

void HtmlGenerator::startParameterName(bool /*oneArgOnly*/)
{
   DBG_HTML(m_textStream << "<!-- startParameterName -->" << endl;)
   m_textStream << "          <td class=\"paramname\">";
}

void HtmlGenerator::endParameterName(bool last, bool emptyList, bool closeBracket)
{
   DBG_HTML(m_textStream << "<!-- endParameterName -->" << endl;)

   if (last) {
      if (emptyList) {
         if (closeBracket) {
            m_textStream << "</td><td>)";
         }
         m_textStream << "</td>" << endl;
         m_textStream << "          <td>";

      } else {
         m_textStream << "&#160;</td>" << endl;
         m_textStream << "        </tr>" << endl;
         m_textStream << "        <tr>" << endl;
         m_textStream << "          <td></td>" << endl;
         m_textStream << "          <td>";
         if (closeBracket) {
            m_textStream << ")";
         }
         m_textStream << "</td>" << endl;
         m_textStream << "          <td></td><td>";
      }

   } else {
      m_textStream << "</td>" << endl;
      m_textStream << "        </tr>" << endl;
   }
}

void HtmlGenerator::endParameterList()
{
   DBG_HTML(m_textStream << "<!-- endParameterList -->" << endl;)

   m_textStream << "</td>" << endl;
   m_textStream << "        </tr>" << endl;
}

void HtmlGenerator::exceptionEntry(const QString &prefix, bool closeBracket)
{
   DBG_HTML(m_textStream << "<!-- exceptionEntry -->" << endl;)
   m_textStream << "</td>" << endl;
   m_textStream << "        </tr>" << endl;
   m_textStream << "        <tr>" << endl;
   m_textStream << "          <td align=\"right\">";

   // colspan 2 so it gets both parameter type and parameter name columns

   if (! prefix.isEmpty()) {
      m_textStream << prefix << "</td><td>(</td><td colspan=\"2\">";

   } else if (closeBracket) {
      m_textStream << "</td><td>)</td><td></td><td>";

   } else {
      m_textStream << "</td><td></td><td colspan=\"2\">";
   }
}

void HtmlGenerator::endMemberDoc(bool hasArgs)
{
   DBG_HTML(m_textStream << "<!-- endMemberDoc -->" << endl;)

   if (!hasArgs) {
      m_textStream << "        </tr>" << endl;
   }

   m_textStream << "      </table>" << endl;
   // m_textStream << "</div>" << endl;
}

void HtmlGenerator::startDotGraph()
{
   startSectionHeader(m_textStream, m_relativePath, m_sectionCount);
}

void HtmlGenerator::endDotGraph(const DotClassGraph &g)
{
   bool generateLegend = Config::getBool("generate-legend");
   bool umlLook = Config::getBool("uml-look");

   endSectionHeader(m_textStream);
   startSectionSummary(m_textStream, m_sectionCount);
   endSectionSummary(m_textStream);
   startSectionContent(m_textStream, m_sectionCount);

   g.writeGraph(m_textStream, GOF_BITMAP, EOF_Html, m_dir, m_fileName, m_relativePath, true, true, m_sectionCount);

   if (generateLegend && ! umlLook) {
      m_textStream << "<center><span class=\"legend\">[";
      startHtmlLink(m_relativePath + "graph_legend" + Doxy_Globals::htmlFileExtension);

      m_textStream << theTranslator->trLegend();
      endHtmlLink();

      m_textStream << "]</span></center>";
   }

   endSectionContent(m_textStream);
   m_sectionCount++;
}

void HtmlGenerator::startInclDepGraph()
{
   startSectionHeader(m_textStream, m_relativePath, m_sectionCount);
}

void HtmlGenerator::endInclDepGraph(const DotInclDepGraph &g)
{
   endSectionHeader(m_textStream);
   startSectionSummary(m_textStream, m_sectionCount);
   endSectionSummary(m_textStream);
   startSectionContent(m_textStream, m_sectionCount);

   g.writeGraph(m_textStream, GOF_BITMAP, EOF_Html, m_dir, m_fileName, m_relativePath, true, m_sectionCount);

   endSectionContent(m_textStream);
   m_sectionCount++;
}

void HtmlGenerator::startGroupCollaboration()
{
   startSectionHeader(m_textStream, m_relativePath, m_sectionCount);
}

void HtmlGenerator::endGroupCollaboration(const DotGroupCollaboration &g)
{
   endSectionHeader(m_textStream);
   startSectionSummary(m_textStream, m_sectionCount);
   endSectionSummary(m_textStream);
   startSectionContent(m_textStream, m_sectionCount);

   g.writeGraph(m_textStream, GOF_BITMAP, EOF_Html, m_dir, m_fileName, m_relativePath, true, m_sectionCount);

   endSectionContent(m_textStream);
   m_sectionCount++;
}

void HtmlGenerator::startCallGraph()
{
   startSectionHeader(m_textStream, m_relativePath, m_sectionCount);
}

void HtmlGenerator::endCallGraph(const DotCallGraph &g)
{
   endSectionHeader(m_textStream);
   startSectionSummary(m_textStream, m_sectionCount);
   endSectionSummary(m_textStream);
   startSectionContent(m_textStream, m_sectionCount);

   g.writeGraph(m_textStream, GOF_BITMAP, EOF_Html, m_dir, m_fileName, m_relativePath, true, m_sectionCount);

   endSectionContent(m_textStream);
   m_sectionCount++;
}

void HtmlGenerator::startDirDepGraph()
{
   startSectionHeader(m_textStream, m_relativePath, m_sectionCount);
}

void HtmlGenerator::endDirDepGraph(const DotDirDeps &g)
{
   endSectionHeader(m_textStream);
   startSectionSummary(m_textStream, m_sectionCount);
   endSectionSummary(m_textStream);
   startSectionContent(m_textStream, m_sectionCount);

   g.writeGraph(m_textStream, GOF_BITMAP, EOF_Html, m_dir, m_fileName, m_relativePath, true, m_sectionCount);

   endSectionContent(m_textStream);
   m_sectionCount++;
}

void HtmlGenerator::writeGraphicalHierarchy(const DotGfxHierarchyTable &g)
{
   g.writeGraph(m_textStream, m_dir, m_fileName);
}

void HtmlGenerator::startMemberGroupHeader(bool)
{
   m_textStream << "<tr><td colspan=\"2\"><div class=\"groupHeader\">";
}

void HtmlGenerator::endMemberGroupHeader()
{
   m_textStream << "</div></td></tr>" << endl;
}

void HtmlGenerator::startMemberGroupDocs()
{
   m_textStream << "<tr><td colspan=\"2\"><div class=\"groupText\">";
}

void HtmlGenerator::endMemberGroupDocs()
{
   m_textStream << "</div></td></tr>" << endl;
}

void HtmlGenerator::startMemberGroup()
{
}

void HtmlGenerator::endMemberGroup(bool)
{
}

void HtmlGenerator::startIndent()
{
   m_textStream << "<div class=\"memdoc\">\n";
}

void HtmlGenerator::endIndent()
{
   m_textStream << endl << "</div>" << endl << "</div>" << endl;
}

void HtmlGenerator::addIndexItem(const QString &, const QString &)
{
}

void HtmlGenerator::writeNonBreakableSpace(int n)
{
   for (int i = 0; i < n; i++) {
      m_textStream << "&#160;";
   }
}

void HtmlGenerator::startSimpleSect(SectionTypes, const QString &filename, const QString &anchor, const QString &title)
{
   m_textStream << "<dl><dt><b>";

   if (! filename.isEmpty()) {
      writeObjectLink(0, filename, anchor, title);

   } else {
      docify(title);
   }

   m_textStream << "</b></dt>";
}

void HtmlGenerator::endSimpleSect()
{
   m_textStream << "</dl>";
}

void HtmlGenerator::startParamList(ParamListTypes, const QString &title)
{
   m_textStream << "<dl><dt><b>";
   docify(title);

   m_textStream << "</b></dt>";
}

void HtmlGenerator::endParamList()
{
   m_textStream << "</dl>";
}

void HtmlGenerator::writeDoc(DocNode *n, QSharedPointer<Definition> ctx, QSharedPointer<MemberDef> md)
{
   assert(m_codeGen);

   HtmlDocVisitor *visitor = new HtmlDocVisitor(m_textStream, *m_codeGen, ctx);
   n->accept(visitor);

   delete visitor;
}

static void startQuickIndexList(QTextStream &t_stream, bool compact, bool topLevel = true)
{
   if (compact) {

      if (topLevel) {
         t_stream << "  <div id=\"navrow1\" class=\"tabs\">\n";

      } else {
         t_stream << "  <div id=\"navrow2\" class=\"tabs2\">\n";
      }

      t_stream << "    <ul class=\"tablist\">\n";

   } else {
      t_stream << "<ul>";
   }
}

static void endQuickIndexList(QTextStream &t_stream, bool compact)
{
   if (compact) {
      t_stream << "    </ul>\n";
      t_stream << "  </div>\n";

   } else {
      t_stream << "</ul>\n";
   }
}

static void startQuickIndexItem(QTextStream &t_stream, const QString &l, bool hl, bool x, const QString &relPath)
{
   t_stream << "      <li";

   if (hl) {
      t_stream << " class=\"current\"";
   }
   t_stream << ">";

   if (! l.isEmpty()) {
      t_stream << "<a href=\"" << correctURL(l, relPath) << "\">";
   }
   t_stream << "<span>";
}

static void endQuickIndexItem(QTextStream &t_stream, const QString &l)
{
   t_stream << "</span>";

   if (! l.isEmpty()) {
      t_stream << "</a>";
   }

   t_stream << "</li>\n";
}

static bool quickLinkVisible(LayoutNavEntry::Kind kind)
{
   static bool showFiles      = Config::getBool("show-file-page");
   static bool showNamespaces = Config::getBool("show-namespace-page");

   switch (kind) {
      case LayoutNavEntry::MainPage:
         return true;

      case LayoutNavEntry::User:
         return true;

      case LayoutNavEntry::UserGroup:
         return true;

      case LayoutNavEntry::Pages:
         return Doxy_Globals::indexedPages > 0;

      case LayoutNavEntry::Modules:
         return documentedGroups > 0;

      case LayoutNavEntry::Namespaces:
         return documentedNamespaces > 0 && showNamespaces;

      case LayoutNavEntry::NamespaceList:
         return documentedNamespaces > 0 && showNamespaces;

      case LayoutNavEntry::NamespaceMembers:
         return documentedNamespaceMembers[NMHL_All] > 0;

      case LayoutNavEntry::Classes:
         return annotatedClasses > 0;

      case LayoutNavEntry::ClassList:
         return annotatedClasses > 0;

      case LayoutNavEntry::ClassIndex:
         return annotatedClasses > 0;

      case LayoutNavEntry::ClassHierarchy:
         return hierarchyClasses > 0;

      case LayoutNavEntry::ClassMembers:
         return documentedClassMembers[CMHL_All] > 0;

      case LayoutNavEntry::Files:
         return Doxy_Globals::documentedHtmlFiles > 0 && showFiles;

      case LayoutNavEntry::FileList:
         return Doxy_Globals::documentedHtmlFiles > 0 && showFiles;

      case LayoutNavEntry::FileGlobals:
         return documentedFileMembers[FMHL_All] > 0;

      case LayoutNavEntry::Examples:
         return Doxy_Globals::exampleSDict.count() > 0;
   }
   return false;
}

static void renderQuickLinksAsTree(QTextStream &t_stream, const QString &relPath, LayoutNavEntry *root)
{
   int count = 0;

   for (auto entry : root->children()) {
      if (entry->visible() && quickLinkVisible(entry->kind())) {
         count++;
      }
   }

   if (count > 0) { // at least one item is visible
      startQuickIndexList(t_stream, false);

      for (auto entry : root->children()) {
         if (entry->visible() && quickLinkVisible(entry->kind())) {
            QString url = entry->url();

            t_stream << "<li><a href=\"" << relPath << url << "\"><span>";
            t_stream << fixSpaces(entry->title());
            t_stream << "</span></a>\n";

            // recursive into child list
            renderQuickLinksAsTree(t_stream, relPath, entry);
            t_stream << "</li>";
         }
      }

      endQuickIndexList(t_stream, false);
   }
}


static void renderQuickLinksAsTabs(QTextStream &t_stream, const QString &relPath, LayoutNavEntry *hlEntry,
                  LayoutNavEntry::Kind kind, bool highlightParent, bool highlightSearch)
{
   if (hlEntry->parent()) {
      // first draw the tabs for the parent of hlEntry
      renderQuickLinksAsTabs(t_stream, relPath, hlEntry->parent(), kind, highlightParent, highlightSearch);
   }

   if (hlEntry->parent() && hlEntry->parent()->children().count() > 0) { // draw tabs for row containing hlEntry
     bool topLevel = hlEntry->parent()->parent() == 0;
     int count = 0;

      for (auto entry : hlEntry->parent()->children()) {
         if (entry->visible() && quickLinkVisible(entry->kind())) {
            count++;
         }
      }

      if (count > 0) {
         // at least one item is visible
         startQuickIndexList(t_stream, true, topLevel);

         for (auto entry : hlEntry->parent()->children()) {
            if (entry->visible() && quickLinkVisible(entry->kind())) {
               QString url = entry->url();

               startQuickIndexItem(t_stream, url, entry == hlEntry  &&
                                   (entry->children().count() > 0 || (entry->kind() == kind && ! highlightParent) ), true, relPath);

               t_stream << fixSpaces(entry->title());
               endQuickIndexItem(t_stream, url);
            }
         }

         if (hlEntry->parent() == LayoutDocManager::instance().rootNavEntry()) { // first row is special as it contains the search box
            static bool searchEngine      = Config::getBool("html-search");
            static bool serverBasedSearch = Config::getBool("search-server-based");

            if (searchEngine) {
               t_stream << "      <li>\n";

               if (!serverBasedSearch) { // pure client side search
                  writeClientSearchBox(t_stream, relPath);
                  t_stream << "      </li>\n";

               } else { // server based search
                  writeServerSearchBox(t_stream, relPath, highlightSearch);
                  if (!highlightSearch) {
                     t_stream << "      </li>\n";
                  }
               }
            }

            if (!highlightSearch) // on the search page the index will be ended by the
               // page itself
            {
               endQuickIndexList(t_stream, true);
            }
         } else { // normal case for other rows than first one
            endQuickIndexList(t_stream, true);
         }
      }
   }
}

static void writeDefaultQuickLinks(QTextStream &t_stream, bool compact, HighlightedItem hli,
                  const QString &file, const QString &relPath)
{
   LayoutNavEntry *root = LayoutDocManager::instance().rootNavEntry();
   LayoutNavEntry::Kind kind = (LayoutNavEntry::Kind) - 1;
   LayoutNavEntry::Kind altKind = (LayoutNavEntry::Kind) - 1; // fall back for the old layout file

   bool highlightParent = false;

   switch (hli) {
      // map HLI enums to LayoutNavEntry::Kind enums

      case HLI_Main:
         kind = LayoutNavEntry::MainPage;
         break;

      case HLI_Modules:
         kind = LayoutNavEntry::Modules;
         break;

      case HLI_Namespaces:
         kind = LayoutNavEntry::NamespaceList;
         altKind = LayoutNavEntry::Namespaces;
         break;
      case HLI_Hierarchy:
         kind = LayoutNavEntry::ClassHierarchy;
         break;

      case HLI_Classes:
         kind = LayoutNavEntry::ClassIndex;
         altKind = LayoutNavEntry::Classes;
         break;

      case HLI_Annotated:
         kind = LayoutNavEntry::ClassList;
         altKind = LayoutNavEntry::Classes;
         break;

      case HLI_Files:
         kind = LayoutNavEntry::FileList;
         altKind = LayoutNavEntry::Files;
         break;

      case HLI_NamespaceMembers:
         kind = LayoutNavEntry::NamespaceMembers;
         break;

      case HLI_Functions:
         kind = LayoutNavEntry::ClassMembers;
         break;

      case HLI_Globals:
         kind = LayoutNavEntry::FileGlobals;
         break;

      case HLI_Pages:
         kind = LayoutNavEntry::Pages;
         break;

      case HLI_FileSource:
         kind = LayoutNavEntry::FileSource;
         break;

      case HLI_Examples:
         kind = LayoutNavEntry::Examples;
         break;

      case HLI_UserGroup:
         kind = LayoutNavEntry::UserGroup;
         break;
      case HLI_ClassVisible:
         kind = LayoutNavEntry::ClassList;
         altKind = LayoutNavEntry::Classes;
         highlightParent = true;
         break;
      case HLI_NamespaceVisible:
         kind = LayoutNavEntry::NamespaceList;
         altKind = LayoutNavEntry::Namespaces;
         highlightParent = true;
         break;
      case HLI_FileVisible:
         kind = LayoutNavEntry::FileList;
         altKind = LayoutNavEntry::Files;
         highlightParent = true;
         break;
      case HLI_None:
         break;
      case HLI_Search:
         break;
   }

   if (compact) {
      // find highlighted index item
      QString temp;

      if (kind == LayoutNavEntry::UserGroup) {
         temp = file;
      }

      LayoutNavEntry *hlEntry = root->find(kind, temp);

      if (! hlEntry && altKind != (LayoutNavEntry::Kind) - 1) {
         hlEntry = root->find(altKind);
         kind = altKind;
      }

      if (! hlEntry) {
         // highlighted item not found in the index! -> just show the level 1 index...
         highlightParent = true;
         hlEntry = root->children().first();

         if (hlEntry == 0) {
            // argl, empty index
            return;
         }
      }

      if (kind == LayoutNavEntry::UserGroup) {
         LayoutNavEntry *e = hlEntry->children().first();
         if (e) {
            hlEntry = e;
         }
      }

      renderQuickLinksAsTabs(t_stream, relPath, hlEntry, kind, highlightParent, hli == HLI_Search);

   } else {
      renderQuickLinksAsTree(t_stream, relPath, root);
   }
}

void HtmlGenerator::endQuickIndices()
{
   m_textStream << "</div><!-- top -->" << endl;
}

QString HtmlGenerator::writeSplitBarAsString(const QString &name, const QString &relpath)
{
   static bool generateTreeView = Config::getBool("generate-treeview");
   QString result;

   // write split bar
   if (generateTreeView) {

      result =    "<div id=\"side-nav\" class=\"ui-resizable side-nav-resizable\">\n"
                  "  <div id=\"nav-tree\">\n"
                  "    <div id=\"nav-tree-contents\">\n"
                  "      <div id=\"nav-sync\" class=\"sync\"></div>\n"
                  "    </div>\n"
                  "  </div>\n"
                  "  <div id=\"splitbar\" style=\"-moz-user-select:none;\" \n"
                  "       class=\"ui-resizable-handle\">\n"
                  "  </div>\n"
                  "</div>\n"
                  "<script type=\"text/javascript\">\n"
                  "$(document).ready(function(){initNavTree('" + name + Doxy_Globals::htmlFileExtension + "','" + relpath +
                  "');});\n</script>\n<div id=\"doc-content\">\n";
   }

   return result;
}

void HtmlGenerator::writeSplitBar(const QString &name)
{
   m_textStream << writeSplitBarAsString(name, m_relativePath);
}

void HtmlGenerator::writeNavigationPath(const QString &s)
{
   m_textStream << substitute(s, "$relpath^", m_relativePath);
}

void HtmlGenerator::startContents()
{
   m_textStream << "<div class=\"contents\">" << endl;
}

void HtmlGenerator::endContents()
{
   m_textStream << "</div><!-- contents -->" << endl;
}

void HtmlGenerator::startPageDoc(const QString &pageTitle)
{
   // RTL    m_textStream << "<div" << getDirHtmlClassOfPage(pageTitle) << ">";
}

void HtmlGenerator::endPageDoc()
{
   // RTL    m_textStream << "</div><!-- PageDoc -->" << endl;
}

void HtmlGenerator::writeQuickLinks(bool compact, HighlightedItem hli, const QString &file)
{
   writeDefaultQuickLinks(m_textStream, compact, hli, file, m_relativePath);
}

// PHP based search script
void HtmlGenerator::writeSearchPage()
{
   static bool generateTreeView  = Config::getBool("generate-treeview");
   static bool disableIndex      = Config::getBool("disable-index");

   static QString projectName = Config::getString("project-name");
   static QString htmlOutput  = Config::getString("html-output");

   // OPENSEARCH_PROVIDER {
   QString configFileName = htmlOutput + "/search_config.php";
   QFile cf(configFileName);

   if (cf.open(QIODevice::WriteOnly)) {

      QTextStream t_stream(&cf);

      t_stream << "<script language=\"php\">\n\n";
      t_stream << "$config = array(\n";
      t_stream << "  'PROJECT_NAME' => \"" << convertToHtml(projectName) << "\",\n";
      t_stream << "  'GENERATE_TREEVIEW' => " << (generateTreeView ? "true" : "false") << ",\n";
      t_stream << "  'DISABLE_INDEX' => " << (disableIndex ? "true" : "false") << ",\n";
      t_stream << ");\n\n";
      t_stream << "$translator = array(\n";
      t_stream << "  'search_results_title' => \"" << theTranslator->trSearchResultsTitle() << "\",\n";
      t_stream << "  'search_results' => array(\n";
      t_stream << "    0 => \"" << theTranslator->trSearchResults(0) << "\",\n";
      t_stream << "    1 => \"" << theTranslator->trSearchResults(1) << "\",\n";
      t_stream << "    2 => \"" << substitute(theTranslator->trSearchResults(2), "$", "\\$") << "\",\n";
      t_stream << "  ),\n";
      t_stream << "  'search_matches' => \"" << theTranslator->trSearchMatches() << "\",\n";
      t_stream << "  'search' => \"" << theTranslator->trSearch() << "\",\n";
      t_stream << "  'split_bar' => \"" << substitute(substitute(writeSplitBarAsString("search", ""), "\"", "\\\""), "\n", "\\n") << "\",\n";
      t_stream << "  'logo' => \"" << substitute(substitute(writeLogoAsString(""), "\"", "\\\""), "\n", "\\n") << "\",\n";
      t_stream << ");\n\n";
      t_stream << "</script>\n";

   } else {
      err("Unable to open file for writing %s, error: %d\n", csPrintable(configFileName), cf.error());

   }

   ResourceMgr::instance().copyResourceAs("html/search_functions.php",  htmlOutput, "search_functions.php");
   ResourceMgr::instance().copyResourceAs("html/search_opensearch.php", htmlOutput, "search_opensearch.php");

   QString fileName = htmlOutput + "/search.php";
   QFile f(fileName);

   if (f.open(QIODevice::WriteOnly)) {
      QTextStream t_stream(&f);

      t_stream << substituteHtmlKeywords(g_header, "Search");

      t_stream << "<!-- " << theTranslator->trGeneratedBy() << " DoxyPress "
        << versionString << " -->" << endl;
      t_stream << "<script type=\"text/javascript\">\n";
      t_stream << "var searchBox = new SearchBox(\"searchBox\", \""
        << "search\",false,'" << theTranslator->trSearch() << "');\n";
      t_stream << "</script>\n";

      if (! Config::getBool("disable-index")) {
         writeDefaultQuickLinks(t_stream, true, HLI_Search, QString(), QString());

      } else {
         t_stream << "</div>" << endl;
      }

      t_stream << "<script language=\"php\">\n";
      t_stream << "require_once \"search_functions.php\";\n";
      t_stream << "main();\n";
      t_stream << "</script>\n";

      // Write empty navigation path, to make footer connect properly
      if (generateTreeView) {
         t_stream << "</div><!-- doc-contents -->\n";
      }

      writePageFooter(t_stream, "Search", QString(), QString());
   }

   QString scriptName = htmlOutput + "/search/search.js";
   QFile sf(scriptName);

   if (sf.open(QIODevice::WriteOnly)) {
      QTextStream t_stream(&sf);
      t_stream << ResourceMgr::instance().getAsString("html/extsearch.js");

   } else {
      err("Unable to open file for writing %s, error: %d\n", csPrintable(scriptName), f.error());
   }
}

void HtmlGenerator::writeExternalSearchPage()
{
   static bool generateTreeView = Config::getBool("generate-treeview");
   QString fileName = Config::getString("html-output") + "/search" + Doxy_Globals::htmlFileExtension;

   QFile f(fileName);

   if (f.open(QIODevice::WriteOnly)) {
      QTextStream t_stream(&f);

      t_stream << substituteHtmlKeywords(g_header, "Search");

      t_stream << "<!-- " << theTranslator->trGeneratedBy() << " DoxyPress "
        << versionString << " -->" << endl;
      t_stream << "<script type=\"text/javascript\">\n";
      t_stream << "var searchBox = new SearchBox(\"searchBox\", \""
        << "search\",false,'" << theTranslator->trSearch() << "');\n";
      t_stream << "</script>\n";

      if (! Config::getBool("disable-index")) {
         writeDefaultQuickLinks(t_stream, true, HLI_Search, QString(), QString());

         t_stream << "            <input type=\"text\" id=\"MSearchField\" name=\"query\" value=\"\" size=\"20\" accesskey=\"S\""
                "onfocus=\"searchBox.OnSearchFieldFocus(true)\" onblur=\"searchBox.OnSearchFieldFocus(false)\"/>\n";

         t_stream << "            </form>\n";
         t_stream << "          </div><div class=\"right\"></div>\n";
         t_stream << "        </div>\n";
         t_stream << "      </li>\n";
         t_stream << "    </ul>\n";
         t_stream << "  </div>\n";
         t_stream << "</div>\n";

      } else {
         t_stream << "</div>" << endl;
      }

      t_stream << writeSplitBarAsString("search", QString());
      t_stream << "<div class=\"header\">" << endl;
      t_stream << "  <div class=\"headertitle\">" << endl;
      t_stream << "    <div class=\"title\">" << theTranslator->trSearchResultsTitle() << "</div>" << endl;
      t_stream << "  </div>" << endl;
      t_stream << "</div>" << endl;
      t_stream << "<div class=\"contents\">" << endl;

      t_stream << "<div id=\"searchresults\"></div>" << endl;
      t_stream << "</div>" << endl;

      if (generateTreeView) {
         t_stream << "</div><!-- doc-contents -->" << endl;
      }

      writePageFooter(t_stream, "Search", QString(), QString());

   } else {
      err("Unable to open file for writing %s, error: %d\n", csPrintable(fileName), f.error());

   }

   QString scriptName = Config::getString("html-output") + "/search/search.js";
   QFile sf(scriptName);

   if (sf.open(QIODevice::WriteOnly)) {
      QTextStream t_stream(&sf);

      t_stream << "var searchResultsText=["
        << "\"" << theTranslator->trSearchResults(0) << "\","
        << "\"" << theTranslator->trSearchResults(1) << "\","
        << "\"" << theTranslator->trSearchResults(2) << "\"];" << endl;

      t_stream << "var serverUrl=\"" << Config::getString("search-external-url") << "\";" << endl;
      t_stream << "var tagMap = {" << endl;

      bool first = true;

      // add search mappings
      const QStringList searchMappings = Config::getList("search-mappings");

      for (auto mapLine : searchMappings) {

         int eqPos = mapLine.indexOf('=');

         if (eqPos != -1) {
            // tag command contains a destination

            QString tagName  = mapLine.left(eqPos).trimmed();
            QString destName = mapLine.right(mapLine.length() - eqPos - 1).trimmed();

            if (! tagName.isEmpty()) {
               if (!first) {
                  t_stream << "," << endl;
               }

               t_stream << "  \"" << tagName << "\": \"" << destName << "\"";
               first = false;
            }
         }

      }

      if (! first) {
         t_stream << endl;
      }

      t_stream << "};" << endl << endl;
      t_stream << ResourceMgr::instance().getAsString("html/extsearch.js");
      t_stream << endl;
      t_stream << "$(document).ready(function() {" << endl;
      t_stream << "  var query = trim(getURLParameter('query'));" << endl;
      t_stream << "  if (query) {" << endl;
      t_stream << "    searchFor(query,0,20);" << endl;
      t_stream << "  } else {" << endl;
      t_stream << "    var results = $('#results');" << endl;
      t_stream << "    results.html('<p>" << theTranslator->trSearchResults(0) << "</p>');" << endl;
      t_stream << "  }" << endl;
      t_stream << "});" << endl;

   } else {
      err("Unable to open file for writing %s, error: %d\n", csPrintable(scriptName), sf.error());
   }
}

void HtmlGenerator::startConstraintList(const QString &header)
{
   m_textStream << "<div class=\"typeconstraint\">" << endl;
   m_textStream << "<dl><dt><b>" << header << "</b></dt>" << endl;
   m_textStream << "<dd>" << endl;
   m_textStream << "<table border=\"0\" cellspacing=\"2\" cellpadding=\"0\">" << endl;
}

void HtmlGenerator::startConstraintParam()
{
   m_textStream << "<tr><td valign=\"top\"><em>";
}

void HtmlGenerator::endConstraintParam()
{
   m_textStream << "</em></td>";
}

void HtmlGenerator::startConstraintType()
{
   m_textStream << "<td>&#160;:</td><td valign=\"top\"><em>";
}

void HtmlGenerator::endConstraintType()
{
   m_textStream << "</em></td>";
}

void HtmlGenerator::startConstraintDocs()
{
   m_textStream << "<td>&#160;";
}

void HtmlGenerator::endConstraintDocs()
{
   m_textStream << "</td></tr>" << endl;
}

void HtmlGenerator::endConstraintList()
{
   m_textStream << "</table>" << endl;
   m_textStream << "</dd>" << endl;
   m_textStream << "</dl>" << endl;
   m_textStream << "</div>" << endl;
}

void HtmlGenerator::lineBreak(const QString &style)
{
   if (! style.isEmpty()) {
      m_textStream << "<br class=\"" << style << "\" />" << endl;

   } else {
      m_textStream << "<br />" << endl;
   }
}

void HtmlGenerator::startHeaderSection()
{
   m_textStream << "<div class=\"header\">" << endl;
}

void HtmlGenerator::startTitleHead(const QString &)
{
   m_textStream << "  <div class=\"headertitle\">" << endl;
   startTitle();
}

void HtmlGenerator::endTitleHead(const QString &, const QString &)
{
   endTitle();
   m_textStream << "  </div>" << endl;
}

void HtmlGenerator::endHeaderSection()
{
   m_textStream << "  <div class=\"clear-floats\"></div>\n";
   m_textStream << "</div><!--header-->" << endl;
}

void HtmlGenerator::startInlineHeader()
{
   if (m_emptySection) {
      m_textStream << "<table class=\"memberdecls\">" << endl;
      m_emptySection = false;
   }

   m_textStream << "<tr><td colspan=\"2\"><h3>";
}

void HtmlGenerator::endInlineHeader()
{
   m_textStream << "</h3></td></tr>" << endl;
}

void HtmlGenerator::startMemberDocSimple()
{
   DBG_HTML(m_textStream << "<!-- startMemberDocSimple -->" << endl;)
   m_textStream << "<table class=\"fieldtable\">" << endl;
   m_textStream << "<tr><th colspan=\"3\">" << theTranslator->trCompoundMembers() << "</th></tr>" << endl;
}

void HtmlGenerator::endMemberDocSimple()
{
   DBG_HTML(m_textStream << "<!-- endMemberDocSimple -->" << endl;)
   m_textStream << "</table>" << endl;
}

void HtmlGenerator::startInlineMemberType()
{
   DBG_HTML(m_textStream << "<!-- startInlineMemberType -->" << endl;)
   m_textStream << "<tr><td class=\"fieldtype\">" << endl;
}

void HtmlGenerator::endInlineMemberType()
{
   DBG_HTML(m_textStream << "<!-- endInlineMemberType -->" << endl;)
   m_textStream << "</td>" << endl;
}

void HtmlGenerator::startInlineMemberName()
{
   DBG_HTML(m_textStream << "<!-- startInlineMemberName -->" << endl;)
   m_textStream << "<td class=\"fieldname\">" << endl;
}

void HtmlGenerator::endInlineMemberName()
{
   DBG_HTML(m_textStream << "<!-- endInlineMemberName -->" << endl;)
   m_textStream << "</td>" << endl;
}

void HtmlGenerator::startInlineMemberDoc()
{
   DBG_HTML(m_textStream << "<!-- startInlineMemberDoc -->" << endl;)
   m_textStream << "<td class=\"fielddoc\">" << endl;
}

void HtmlGenerator::endInlineMemberDoc()
{
   DBG_HTML(m_textStream << "<!-- endInlineMemberDoc -->" << endl;)
   m_textStream << "</td></tr>" << endl;
}

void HtmlGenerator::startLabels()
{
   DBG_HTML(m_textStream << "<!-- startLabels -->" << endl;)
   m_textStream << "<span class=\"mlabels\">";
}

void HtmlGenerator::writeLabel(const QString &l, bool)
{
   DBG_HTML(m_textStream << "<!-- writeLabel(" << l << ") -->" << endl;)
   m_textStream << "<span class=\"mlabel\">" << l << "</span>";
}

void HtmlGenerator::endLabels()
{
   DBG_HTML(m_textStream << "<!-- endLabels -->" << endl;)
   m_textStream << "</span>";
}

void HtmlGenerator::writeInheritedSectionTitle(const QString &id, const QString &ref, const QString &file, const QString &anchor,
                                               const QString &title, const QString &name)
{
   DBG_HTML(m_textStream << "<!-- writeInheritedSectionTitle -->" << endl;)
   QString a = anchor;

   if (! a.isEmpty()) {
      a.prepend("#");
   }

   QString classLink = "<a class=\"el\" href=\"";

   if (! ref.isEmpty()) {
      classLink += externalLinkTarget() + externalRef(m_relativePath, ref, true);
   } else {
      classLink += m_relativePath;
   }

   classLink += file + Doxy_Globals::htmlFileExtension + a;
   classLink += "\">" + convertToHtml(name, false) + "</a>";

   m_textStream << "<tr class=\"inherit_header " << id << "\">"
     << "<td colspan=\"2\" onclick=\"javascript:toggleInherit('" << id << "')\">"
     << "<img src=\"" << m_relativePath << "closed.png\" alt=\"-\"/>&#160;"
     << theTranslator->trInheritedFrom(convertToHtml(title, false), classLink)
     << "</td></tr>" << endl;
}

void HtmlGenerator::writeSummaryLink(const QString &file, const QString &anchor, const QString &title, bool first)
{
   if (first) {
      m_textStream << "  <div class=\"summary\">\n";

   } else {
      m_textStream << " &#124;\n";

   }

   m_textStream << "<a href=\"";

   if (! file.isEmpty()) {
      m_textStream << m_relativePath << file;
      m_textStream << Doxy_Globals::htmlFileExtension;

   } else {
      m_textStream << "#";
      m_textStream << anchor;
   }

   m_textStream << "\">";
   m_textStream << title;
   m_textStream << "</a>";
}

void HtmlGenerator::endMemberDeclaration(const QString &anchor, const QString &inheritId)
{
   m_textStream << "<tr class=\"separator:" << anchor;

   if (! inheritId.isEmpty() ) {
      m_textStream << " inherit " << inheritId;
   }

   m_textStream << "\"><td class=\"memSeparator\" colspan=\"2\">&#160;</td></tr>\n";
}

void HtmlGenerator::setCurrentDoc(QSharedPointer<Definition> context, const QString &anchor, bool isSourceFile)
{
   if (Doxy_Globals::searchIndexBase != nullptr) {
      Doxy_Globals::searchIndexBase->setCurrentDoc(context, anchor, isSourceFile);
   }
}

void HtmlGenerator::addWord(const QString &word, bool hiPriority)
{
   if (Doxy_Globals::searchIndexBase != nullptr) {
      Doxy_Globals::searchIndexBase->addWord(word, hiPriority);
   }
}

