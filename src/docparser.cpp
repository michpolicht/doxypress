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

#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <QStack>
#include <QHash>
#include <QRegExp>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <doxygen.h>
#include <util.h>
#include <pagedef.h>
#include <docparser.h>
#include <doctokenizer.h>
#include <cmdmapper.h>
#include <printdocvisitor.h>
#include <message.h>
#include <section.h>
#include <searchindex.h>
#include <language.h>
#include <portable.h>
#include <cite.h>
#include <arguments.h>
#include <groupdef.h>
#include <classlist.h>
#include <filedef.h>
#include <memberdef.h>
#include <namespacedef.h>
#include <reflist.h>
#include <formula.h>
#include <config.h>
#include <growbuf.h>
#include <markdown.h>
#include <htmlentity.h>

// must appear after the previous include - resolve soon 
#include <doxy_globals.h>

// debug off
#define DBG(x) do {} while(0)

// debug to stdout
//#define DBG(x) printf x

// debug to stderr
//#define myprintf(x...) fprintf(stderr,x)
//#define DBG(x) myprintf x

#define INTERNAL_ASSERT(x) do {} while(0)
//#define INTERNAL_ASSERT(x) if (!(x)) DBG(("INTERNAL_ASSERT(%s) failed retval=0x%x: file=%s line=%d\n",#x,retval,__FILE__,__LINE__));


static bool defaultHandleToken(DocNode *parent, int tok, QList<DocNode *> &children, bool handleWord = true);

static const char *sectionLevelToName[] = {
   "page",
   "section",
   "subsection",
   "subsubsection",
   "paragraph",
   "subparagraph"
};

/** Parser's context to store all global variables.
 */
struct DocParserContext {
   Definition *scope;
   QByteArray context;
   bool inSeeBlock;
   bool xmlComment;
   bool insideHtmlLink;

   QStack<DocNode *> nodeStack;
   QStack<DocStyleChange> styleStack;
   QStack<DocStyleChange> initialStyleStack;
   QList<Definition *> copyStack;

   QByteArray fileName;
   QByteArray relPath;

   bool hasParamCommand;
   bool hasReturnCommand;

   MemberDef *memberDef;

   QHash<QString, void *>  paramsFound;

   bool isExample;
   QByteArray   exampleName;
   SectionDict *sectionDict;
   QByteArray   searchUrl;

   QByteArray  includeFileText;

   uint includeFileOffset;
   uint includeFileLength;

   TokenInfo *token;
};

// Parser state variables during a call to validatingParseDoc
static Definition             *s_scope;
static QByteArray              s_context;
static bool                    s_inSeeBlock;
static bool                    s_xmlComment;
static bool                    s_insideHtmlLink;
static QStack<DocNode *>       s_nodeStack;
static QStack<DocStyleChange>  s_styleStack;
static QStack<DocStyleChange>  s_initialStyleStack;
static QList<Definition *>     s_copyStack;
static QByteArray              s_fileName;
static QByteArray              s_relPath;

static bool                    s_hasParamCommand;
static bool                    s_hasReturnCommand;
static QHash<QString, void *>  s_paramsFound;
static MemberDef              *s_memberDef;
static bool                    s_isExample;
static QByteArray              s_exampleName;
static SectionDict            *s_sectionDict;
static QByteArray              s_searchUrl;

static QByteArray              s_includeFileText;
static uint                    s_includeFileOffset;
static uint                    s_includeFileLength;

static QStack<DocParserContext> s_parserStack;

static void docParserPushContext(bool saveParamInfo = true)
{  
   doctokenizerYYpushContext();

   DocParserContext ctx;

   ctx.scope              = s_scope;
   ctx.context            = s_context;
   ctx.inSeeBlock         = s_inSeeBlock;
   ctx.xmlComment         = s_xmlComment;
   ctx.insideHtmlLink     = s_insideHtmlLink;
   ctx.nodeStack          = s_nodeStack;
   ctx.styleStack         = s_styleStack;
   ctx.initialStyleStack  = s_initialStyleStack;
   ctx.copyStack          = s_copyStack;
   ctx.fileName           = s_fileName;
   ctx.relPath            = s_relPath;

   if (saveParamInfo) {
      ctx.hasParamCommand    = s_hasParamCommand;
      ctx.hasReturnCommand   = s_hasReturnCommand;
      ctx.paramsFound        = s_paramsFound;
   }

   ctx.memberDef          = s_memberDef;
   ctx.isExample          = s_isExample;
   ctx.exampleName        = s_exampleName;
   ctx.sectionDict        = s_sectionDict;
   ctx.searchUrl          = s_searchUrl;

   ctx.includeFileText    = s_includeFileText;
   ctx.includeFileOffset  = s_includeFileOffset;
   ctx.includeFileLength  = s_includeFileLength;

   ctx.token              = g_token;
   g_token = new TokenInfo;

   s_parserStack.push(ctx);
}

static void docParserPopContext(bool keepParamInfo = false)
{
   DocParserContext ctx = s_parserStack.pop();

   s_scope               = ctx.scope;
   s_context             = ctx.context;
   s_inSeeBlock          = ctx.inSeeBlock;
   s_xmlComment          = ctx.xmlComment;
   s_insideHtmlLink      = ctx.insideHtmlLink;
   s_nodeStack           = ctx.nodeStack;
   s_styleStack          = ctx.styleStack;
   s_initialStyleStack   = ctx.initialStyleStack;
   s_copyStack           = ctx.copyStack;
   s_fileName            = ctx.fileName;
   s_relPath             = ctx.relPath;

   if (!keepParamInfo) {
      s_hasParamCommand     = ctx.hasParamCommand;
      s_hasReturnCommand    = ctx.hasReturnCommand;
      s_paramsFound         = ctx.paramsFound;
   }

   s_memberDef           = ctx.memberDef;
   s_isExample           = ctx.isExample;
   s_exampleName         = ctx.exampleName;
   s_sectionDict         = ctx.sectionDict;
   s_searchUrl           = ctx.searchUrl;

   s_includeFileText     = ctx.includeFileText;
   s_includeFileOffset   = ctx.includeFileOffset;
   s_includeFileLength   = ctx.includeFileLength;

   delete g_token;
   g_token               = ctx.token;
  
   doctokenizerYYpopContext();   
}

/*! search for an image in the imageNameDict and if found
 * copies the image to the output directory (which depends on the \a type
 * parameter).
 */
static QByteArray findAndCopyImage(const char *fileName, DocImage::Type type)
{
   QByteArray result;
   bool ambig;
   FileDef *fd;
 
   if ((fd = findFileDef(Doxygen::imageNameDict, fileName, ambig))) {
      QByteArray inputFile = fd->getFilePath();
      QFile inImage(inputFile);

      if (inImage.open(QIODevice::ReadOnly)) {
         result = fileName;
         int i;

         if ((i = result.lastIndexOf('/')) != -1 || (i = result.lastIndexOf('\\')) != -1) {
            result = result.right(result.length() - i - 1);
         }
         
         QByteArray outputDir;

         switch (type) {
            case DocImage::Html:
               if (!Config_getBool("GENERATE_HTML")) {
                  return result;
               }
               outputDir = Config_getString("HTML_OUTPUT");
               break;
            case DocImage::Latex:
               if (!Config_getBool("GENERATE_LATEX")) {
                  return result;
               }
               outputDir = Config_getString("LATEX_OUTPUT");
               break;
            case DocImage::DocBook:
               if (!Config_getBool("GENERATE_DOCBOOK")) {
                  return result;
               }
               outputDir = Config_getString("DOCBOOK_OUTPUT");
               break;
            case DocImage::Rtf:
               if (!Config_getBool("GENERATE_RTF")) {
                  return result;
               }
               outputDir = Config_getString("RTF_OUTPUT");
               break;
         }

         QByteArray outputFile = outputDir + "/" + result;
         QFileInfo outfi(outputFile);

         if (outfi.isSymLink()) {
            QFile::remove(outputFile);

            warn_doc_error(s_fileName, doctokenizerYYlineno, "destination of image %s is a symlink, replacing with image", qPrint(outputFile));
         }

         if (outputFile != inputFile) { 
            // prevent copying to ourself
            QFile outImage(outputFile.data());

            if (outImage.open(QIODevice::WriteOnly)) { 
               // copy the image
               char *buffer = new char[inImage.size()];
               inImage.read(buffer, inImage.size());

               outImage.write(buffer, inImage.size());
               outImage.flush();

               delete[] buffer;

               if (type == DocImage::Html) {
                  Doxygen::indexList->addImageFile(result);
               }

            } else {
               warn_doc_error(s_fileName, doctokenizerYYlineno, "could not write output image %s", qPrint(outputFile));
            }
         } else {
            printf("Source & Destination are the same!\n");
         }

      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "could not open image %s", qPrint(fileName));
      }

      if (type == DocImage::Latex && Config_getBool("USE_PDFLATEX") && fd->name().right(4) == ".eps") {
         // we have an .eps image in pdflatex mode => convert it to a pdf.

         QByteArray outputDir = Config_getString("LATEX_OUTPUT");
         QByteArray baseName  = fd->name().left(fd->name().length() - 4);
                             
         QString epstopdfArgs;
         epstopdfArgs = QString("\"%1/%2.eps\" --outfile=\"%3/%4.pdf\"").arg(outputDir.constData()).arg(baseName.constData()).
                                                                         arg(outputDir.constData()).arg(baseName.constData());

         portable_sysTimerStart();

         if (portable_system("epstopdf", qPrintable(epstopdfArgs)) != 0) {
            err("Problems running epstopdf. Check your TeX installation!\n");
         }

         portable_sysTimerStop();
         return baseName;
      }

   } else if (ambig) {
      QString text;
      text = QString("Image file name %1 is ambiguous.\n").arg(fileName);

      text += "Possible candidates:\n";
      text += showFileDefMatches(Doxygen::imageNameDict, fileName);

      warn_doc_error(s_fileName, doctokenizerYYlineno, text);

   } else {
      result = fileName;

      if (result.left(5) != "http:" && result.left(6) != "https:") {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "image file %s is not found in IMAGE_PATH: "
                        "assuming external image.", qPrint(fileName) );
      }
   }
   return result;
}

/*! Collects the parameters found with \@param or \@retval commands
 *  in a global list s_paramsFound. If \a isParam is set to true
 *  and the parameter is not an actual parameter of the current
 *  member g_memberDef, then a warning is raised (unless warnings
 *  are disabled altogether).
 */
static void checkArgumentName(const QByteArray &name, bool isParam)
{
   if (!Config_getBool("WARN_IF_DOC_ERROR")) {
      return;
   }

   if (s_memberDef == 0) {
      return; 
   }

   ArgumentList *al = s_memberDef->isDocsForDefinition() ? s_memberDef->argumentList() : s_memberDef->declArgumentList();
   SrcLangExt lang = s_memberDef->getLanguage();
 
   if (al == 0) {
      return;  
   }

   static QRegExp re("$?[a-zA-Z0-9_\\x80-\\xFF]+\\.*");
   int p = 0;
   int i = 0;
   int l;

   while ((i = re.indexIn(name, p)) != -1) { 
      // to handle @param x,y   
      l = re.matchedLength();

      QByteArray aName = name.mid(i, l);

      if (lang == SrcLangExt_Fortran) {
         aName = aName.toLower();
      }          

      bool found = false;
    
      for (auto a : *al) {
         QByteArray argName = s_memberDef->isDefine() ? a.type : a.name;

         if (lang == SrcLangExt_Fortran) {
            argName = argName.toLower();
         }

         argName = argName.trimmed();
         
         if (argName.right(3) == "...") {
            argName = argName.left(argName.length() - 3);
         }

         if (aName == argName) {
            s_paramsFound.insert(aName, (void *)(0x8));
            found = true;
            break;
         }
      }

      if (!found && isParam) {         
         QByteArray scope = s_memberDef->getScopeString();

         if (!scope.isEmpty()) {
            scope += "::";
         } else {
            scope = "";
         }

         QString inheritedFrom = "";

         QByteArray docFile = s_memberDef->docFile();
         int docLine = s_memberDef->docLine();

         MemberDef *inheritedMd = s_memberDef->inheritsDocsFrom();

         if (inheritedMd) { 
            // documentation was inherited

            inheritedFrom = QString(" inherited from member %1 at line %2 in file %3").arg(QString(inheritedMd->name())).
                                  arg(inheritedMd->docLine()).arg(QString(inheritedMd->docFile()));
  
            docFile = s_memberDef->getDefFileName();
            docLine = s_memberDef->getDefLine();
         }

         QByteArray alStr = argListToString(al);
         warn_doc_error(docFile, docLine, "argument '%s' of command @param is not found in the argument list of %s%s%s%s",
                        qPrint(aName), qPrint(scope), qPrint(s_memberDef->name()),
                        qPrint(alStr), qPrintable(inheritedFrom));
      }

      p = i + l;
   }
}

/*! Checks if the parameters that have been specified using \@param are
 *  indeed all parameters.
 *  Must be called after checkArgumentName() has been called for each
 *  argument.
 */
static void checkUndocumentedParams()
{
   if (s_memberDef && s_hasParamCommand && Config_getBool("WARN_IF_DOC_ERROR")) {
      ArgumentList *al = s_memberDef->isDocsForDefinition() ? s_memberDef->argumentList() : s_memberDef->declArgumentList();

      SrcLangExt lang = s_memberDef->getLanguage();

      if (al != 0) {    
         bool found = false;
        
         for (auto a : *al) { 
            QByteArray argName = s_memberDef->isDefine() ? a.type : a.name;
            if (lang == SrcLangExt_Fortran) {
               argName = argName.toLower();
            }

            argName = argName.trimmed();
            if (argName.right(3) == "...") {
               argName = argName.left(argName.length() - 3);
            }

            if (s_memberDef->getLanguage() == SrcLangExt_Python && argName == "self") {
               // allow undocumented self parameter for Python

            } else if (!argName.isEmpty() && ! s_paramsFound.contains(argName) && a.docs.isEmpty()) {
               found = true;
               break;
            }
         }

         if (found) {
            bool first = true;

            QByteArray errMsg = "The following parameters of " + QByteArray(s_memberDef->qualifiedName()) +
               QByteArray(argListToString(al)) + " are not documented:\n";

            for (auto a : *al) { 
               QByteArray argName = s_memberDef->isDefine() ? a.type : a.name;

               if (lang == SrcLangExt_Fortran) {
                  argName = argName.toLower();
               }

               argName = argName.trimmed();

               if (s_memberDef->getLanguage() == SrcLangExt_Python && argName == "self") {
                  // allow undocumented self parameter for Python

               } else if (!argName.isEmpty() && ! s_paramsFound.contains(argName)) {

                  if (! first) {
                     errMsg += "\n";

                  } else {
                     first = false;

                  }

                  errMsg += "  parameter '" + argName + "'";
               }
            }

            if (s_memberDef->inheritsDocsFrom()) {
               warn_doc_error(s_memberDef->getDefFileName(), s_memberDef->getDefLine(), qPrintable(substitute(errMsg, "%", "%%")));

            } else {
               warn_doc_error(s_memberDef->docFile(), s_memberDef->docLine(), qPrintable(substitute(errMsg, "%", "%%")));
            }
         }
      }
   }
}

/*! Check if a member has documentation for its parameter and or return
 *  type, if applicable. If found this will be stored in the member, this
 *  is needed as a member can have brief and detailed documentation, while
 *  only one of these needs to document the parameters.
 */
static void detectNoDocumentedParams()
{
   if (s_memberDef && Config_getBool("WARN_NO_PARAMDOC")) {
      ArgumentList *al     = s_memberDef->argumentList();
      ArgumentList *declAl = s_memberDef->declArgumentList();

      QByteArray returnType   = s_memberDef->typeString();
      bool isPython = s_memberDef->getLanguage() == SrcLangExt_Python;

      if (! s_memberDef->hasDocumentedParams() && s_hasParamCommand) {         
         s_memberDef->setHasDocumentedParams(true);

      } else if (!s_memberDef->hasDocumentedParams()) {
         bool allDoc = true; // no paramater => all parameters are documented

         if ( al != 0 && al->count() > 0) {    
            // member has parameters but the member has a parameter list
            // with at least one parameter (that is not void)
         
            // see if all parameters have documentation          
            for (auto a : *al) {  
               if (! allDoc) {
                  break;
               }

               if (! a.name.isEmpty() && a.type != "void" && !(isPython && a.name == "self")) {
                  allDoc = ! a.docs.isEmpty();
               }
            }

            if (!allDoc && declAl != 0) { 
               // try declaration arguments as well
               allDoc = true;
              
               for (auto a : *declAl) { 

                  if (! allDoc) {
                     break;
                  }

                  if (! a.name.isEmpty() && a.type != "void" && !(isPython && a.name == "self")) {
                     allDoc = ! a.docs.isEmpty();
                  }                  
               }
            }
         }

         if (allDoc) {
            s_memberDef->setHasDocumentedParams(true);
         }
      }

      if (! s_memberDef->hasDocumentedReturnType() && s_hasReturnCommand) {
         // docs not yet found
         s_memberDef->setHasDocumentedReturnType(true);

      } else if ( // see if return needs to documented 
         s_memberDef->hasDocumentedReturnType() || returnType.isEmpty() || returnType.indexOf("void") != -1 || 
               returnType.indexOf("subroutine") != -1 || s_memberDef->isConstructor() ||  s_memberDef->isDestructor() ) {

         s_memberDef->setHasDocumentedReturnType(true);
      }

   }
}

/*! Strips known html and tex extensions from \a text. */
static QByteArray stripKnownExtensions(const char *text)
{
   QByteArray result = text;

   if (result.right(4) == ".tex") {
      result = result.left(result.length() - 4);

   } else if (result.right(Doxygen::htmlFileExtension.length()) == QByteArray(Doxygen::htmlFileExtension)) {
      result = result.left(result.length() - Doxygen::htmlFileExtension.length());
   }

   return result;
}


/*! Returns true iff node n is a child of a preformatted node */
static bool insidePRE(DocNode *n)
{
   while (n) {
      if (n->isPreformatted()) {
         return true;
      }
      n = n->parent();
   }

   return false;
}

/*! Returns true iff node n is a child of a html list item node */
static bool insideLI(DocNode *n)
{
   while (n) {
      if (n->kind() == DocNode::Kind_HtmlListItem) {
         return true;
      }
      n = n->parent();
   }
   return false;
}

/*! Returns true iff node n is a child of a unordered html list node */
static bool insideUL(DocNode *n)
{
   while (n) {
      if (n->kind() == DocNode::Kind_HtmlList && ((DocHtmlList *)n)->type() == DocHtmlList::Unordered) {
         return true;
      }
      n = n->parent();
   }
   return false;
}

/*! Returns true iff node n is a child of a ordered html list node */
static bool insideOL(DocNode *n)
{
   while (n) {
      if (n->kind() == DocNode::Kind_HtmlList && ((DocHtmlList *)n)->type() == DocHtmlList::Ordered) {
         return true;
      }
      n = n->parent();
   }
   return false;
}

static bool insideTable(DocNode *n)
{
   while (n) {
      if (n->kind() == DocNode::Kind_HtmlTable) {
         return true;
      }
      n = n->parent();
   }
   return false;
}

/*! Looks for a documentation block with name commandName in the current
 *  context (g_context). The resulting documentation string is
 *  put in pDoc, the definition in which the documentation was found is
 *  put in pDef.
 *  @retval true if name was found.
 *  @retval false if name was not found.
 */
static bool findDocsForMemberOrCompound(const char *commandName, QByteArray *pDoc, QByteArray *pBrief, Definition **pDef)
{
   //printf("findDocsForMemberOrCompound(%s)\n",commandName);
   *pDoc   = "";
   *pBrief = "";
   *pDef   = 0;

   QByteArray cmdArg = substitute(commandName, "#", "::");
   int l = cmdArg.length();

   if (l == 0) {
      return false;
   }

   int funcStart = cmdArg.indexOf('(');

   if (funcStart == -1) {
      funcStart = l;

   } else {
      // Check for the case of operator() and the like.
      // beware of scenarios like operator()((foo)bar)
      int secondParen = cmdArg.indexOf('(', funcStart + 1);
      int leftParen   = cmdArg.indexOf(')', funcStart + 1);

      if (leftParen != -1 && secondParen != -1) {
         if (leftParen < secondParen) {
            funcStart = secondParen;
         }
      }
   }

   QByteArray name = removeRedundantWhiteSpace(cmdArg.left(funcStart));
   QByteArray args = cmdArg.right(l - funcStart);

   // try if the link is to a member
   MemberDef    *md = 0;
   FileDef      *fd = 0;

   QSharedPointer<ClassDef> cd; 
   QSharedPointer<NamespaceDef> nd;
   QSharedPointer<GroupDef> gd;
   QSharedPointer<PageDef> pd;
 
   ClassDef *cd_Temp; 
   NamespaceDef *nd_Temp;
   GroupDef *gd_Temp;
   PageDef *pd_Temp;
  
   // `find('.') is a hack to detect files
   bool found = getDefs(s_context.indexOf('.') == -1 ? s_context.data() : "", name, args.isEmpty() ? 0 : args.data(), 
                        md, cd_Temp, fd, nd_Temp, gd_Temp, false, 0, true);

   cd = QSharedPointer<ClassDef>(cd_Temp, [](ClassDef *){});
   nd = QSharedPointer<NamespaceDef>(nd_Temp, [](NamespaceDef *){}) ;
   gd = QSharedPointer<GroupDef>(gd_Temp, [](GroupDef *){}) ;
   pd = QSharedPointer<PageDef>(pd_Temp, [](PageDef *){}) ;
   
   if (found && md) {
      *pDoc = md->documentation();
      *pBrief = md->briefDescription();
      *pDef = md;

      return true;
   }

   int scopeOffset = s_context.length();
   do { 
      // for each scope
      QByteArray fullName = cmdArg;

      if (scopeOffset > 0) {
         fullName.prepend(s_context.left(scopeOffset) + "::");
      }

      // try class, namespace, group, page, file reference
      cd = Doxygen::classSDict->find(fullName);
      if (cd) { // class
         *pDoc = cd->documentation();
         *pBrief = cd->briefDescription();
         *pDef = cd.data();

         return true;
      }

      nd = Doxygen::namespaceSDict->find(fullName);
      if (nd) { // namespace
         *pDoc = nd->documentation();
         *pBrief = nd->briefDescription();
         *pDef = nd.data();
         return true;
      }

      gd = Doxygen::groupSDict->find(cmdArg);
      if (gd) { // group
         *pDoc = gd->documentation();
         *pBrief = gd->briefDescription();
         *pDef = gd.data();
         return true;
      }

      pd = Doxygen::pageSDict->find(cmdArg);
      if (pd) { // page
         *pDoc = pd->documentation();
         *pBrief = pd->briefDescription();
         *pDef = pd.data();
         return true;
      }

      bool ambig;
      fd = findFileDef(Doxygen::inputNameDict, cmdArg, ambig);
      if (fd && !ambig) { // file
         *pDoc = fd->documentation();
         *pBrief = fd->briefDescription();
         *pDef = fd;
         return true;
      }

      if (scopeOffset == 0) {
         scopeOffset = -1;

      } else {
         scopeOffset = s_context.lastIndexOf("::", scopeOffset - 1);
         if (scopeOffset == -1) {
            scopeOffset = 0;
         }
      }

   } while (scopeOffset >= 0);


   return false;
}

static int handleStyleArgument(DocNode *parent, QList<DocNode *> &children, const QByteArray &cmdName)
{
   DBG(("handleStyleArgument(%s)\n", qPrint(cmdName)));

   QByteArray tokenName = g_token->name;
   int tok = doctokenizerYYlex();

   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command", qPrint(cmdName));
      return tok;
   }

   while ((tok = doctokenizerYYlex()) && tok != TK_WHITESPACE && tok != TK_NEWPARA && 
          tok != TK_LISTITEM && tok != TK_ENDLIST) {

      static QRegExp specialChar("[.,|()\\[\\]:;\\?]");

      if (tok == TK_WORD && g_token->name.length() == 1 && specialChar.indexIn(g_token->name) != -1) {
         // special character that ends the markup command
         return tok;
      }

      if (!defaultHandleToken(parent, tok, children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command \\%s as the argument of a \\%s command",
                              qPrint(g_token->name), qPrint(cmdName));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found while handling command %s",
                              qPrint(g_token->name), qPrint(cmdName));
               break;
            case TK_HTMLTAG:
               if (insideLI(parent) && Mappers::htmlTagMapper->map(g_token->name) && g_token->endTag) {
                  // ignore </li> as the end of a style command
                  continue;
               }

               return tok;
               break;

            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s while handling command %s",
                              tokToString(tok), qPrint(cmdName));
               break;
         }
         break;
      }
   }

   DBG(("handleStyleArgument(%s) end tok=%x\n", qPrint(cmdName), tok));

   return (tok == TK_NEWPARA || tok == TK_LISTITEM || tok == TK_ENDLIST) ? tok : RetVal_OK;
}

/*! Called when a style change starts. For instance a \<b\> command is
 *  encountered.
 */
static void handleStyleEnter(DocNode *parent, QList<DocNode *> &children, DocStyleChange::Style s, const HtmlAttribList *attribs)
{
   DBG(("HandleStyleEnter\n"));

   DocStyleChange *sc = new DocStyleChange(parent, s_nodeStack.count(), s, true, attribs);
   children.append(sc);

   s_styleStack.push(*sc);
}

/*! Called when a style change ends. For instance a \</b\> command is
 *  encountered.
 */
static void handleStyleLeave(DocNode *parent, QList<DocNode *> &children, DocStyleChange::Style s, const char *tagName)
{
   DBG(("HandleStyleLeave\n"));

   if (s_styleStack.isEmpty() || s_styleStack.top().style() != s || s_styleStack.top().position() != s_nodeStack.count()) { 

      if (s_styleStack.isEmpty()) { 
         warn_doc_error(s_fileName, doctokenizerYYlineno, "found </%s> tag without matching <%s>",
                        qPrint(tagName), qPrint(tagName));

      } else if (s_styleStack.top().style() != s) {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "found </%s> tag while expecting </%s>",
                        qPrint(tagName), qPrint(s_styleStack.top().styleString()));

      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "found </%s> at different nesting level (%d) than expected (%d)",
                        qPrint(tagName), s_nodeStack.count(), s_styleStack.top().position());
      }

   } else { 
      // end the section
      DocStyleChange *sc = new DocStyleChange(parent, s_nodeStack.count(), s, false);

      children.append(sc);
      s_styleStack.pop();
   }
}

/*! Called at the end of a paragraph to close all open style changes
 *  (e.g. a <b> without a </b>). The closed styles are pushed onto a stack
 *  and entered again at the start of a new paragraph.
 */
static void handlePendingStyleCommands(DocNode *parent, QList<DocNode *> &children)
{
   if (! s_styleStack.isEmpty()) {
      DocStyleChange sc = s_styleStack.top();

      while (sc.position() >= s_nodeStack.count()) {
         // there are unclosed style modifiers in the paragraph
         children.append(new DocStyleChange(parent, s_nodeStack.count(), sc.style(), false));
        
         s_initialStyleStack.push(sc);

         if (s_styleStack.isEmpty()) {
            break;
         }

         s_styleStack.pop();
         sc = s_styleStack.top();
      }
   }
}

static void handleInitialStyleCommands(DocPara *parent, QList<DocNode *> &children)
{ 
   while (! s_initialStyleStack.isEmpty()) {
      DocStyleChange sc = s_initialStyleStack.pop();
      handleStyleEnter(parent, children, sc.style(), &sc.attribs());
   }
}

static int handleAHref(DocNode *parent, QList<DocNode *> &children, const HtmlAttribList &tagHtmlAttribs)
{   
   int index  = 0;
   int retval = RetVal_OK;

   for (auto opt : tagHtmlAttribs) { 
         
      if (opt.name == "name") { // <a name=label> tag

         if (! opt.value.isEmpty()) {
            DocAnchor *anc = new DocAnchor(parent, opt.value, true);
            children.append(anc);
            break; // stop looking for other tag attribs

         } else {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "found <a> tag with name option but without value!");
         }

      } else if (opt.name == "href") { // <a href=url>..</a> tag
         // copy attributes
         HtmlAttribList attrList = tagHtmlAttribs;

         // and remove the href attribute
         attrList.removeAt(index);
         
         DocHRef *href = new DocHRef(parent, attrList, opt.value, s_relPath);
         children.append(href);

         s_insideHtmlLink = true;
         retval = href->parse();
         s_insideHtmlLink = false;

         break;

      } else { 
         // unsupported option for tag a

      }

      ++index;
   }

   return retval;
}

const char *DocStyleChange::styleString() const
{
   switch (m_style) {
      case DocStyleChange::Bold:
         return "b";
      case DocStyleChange::Italic:
         return "em";
      case DocStyleChange::Code:
         return "code";
      case DocStyleChange::Center:
         return "center";
      case DocStyleChange::Small:
         return "small";
      case DocStyleChange::Subscript:
         return "subscript";
      case DocStyleChange::Superscript:
         return "superscript";
      case DocStyleChange::Preformatted:
         return "pre";
      case DocStyleChange::Div:
         return "div";
      case DocStyleChange::Span:
         return "span";
   }
   return "<invalid>";
}

static void handleUnclosedStyleCommands()
{
   if (! s_initialStyleStack.isEmpty()) {
      DocStyleChange sc = s_initialStyleStack.top();

      s_initialStyleStack.pop();
      handleUnclosedStyleCommands();

      warn_doc_error(s_fileName, doctokenizerYYlineno, "end of comment block while expecting "
                     "command </%s>", qPrint(sc.styleString()));
   }
}

static void handleLinkedWord(DocNode *parent, QList<DocNode *> &children)
{
   QByteArray name = linkToText(SrcLangExt_Unknown, g_token->name, true);
   static bool autolinkSupport = Config_getBool("AUTOLINK_SUPPORT");

   if (! autolinkSupport) { 
      // no autolinking -> add as normal word
      children.append(new DocWord(parent, name));
      return;
   }

   // ------- try to turn the word 'name' into a link

   Definition *compound = 0;
   MemberDef  *member = 0;

   int len = g_token->name.length();

   ClassDef *cd = 0;
   bool ambig;

   FileDef *fd = findFileDef(Doxygen::inputNameDict, s_fileName, ambig);
 
   if (! s_insideHtmlLink && (resolveRef(s_context, g_token->name, s_inSeeBlock, &compound, &member, true, fd, true)
          || (!s_context.isEmpty() &&  // also try with global scope
              resolveRef("", g_token->name, s_inSeeBlock, &compound, &member, false, 0, true)) )) {

     if (member && member->isLinkable()) { 

         if (member->isObjCMethod()) {
            bool localLink = s_memberDef ? member->getClassDef() == s_memberDef->getClassDef() : false;
            name = member->objCMethodName(localLink, s_inSeeBlock); 
         }

         children.append(new DocLinkedWord(parent, name, member->getReference(), member->getOutputFileBase(),
                                           member->anchor(), member->briefDescriptionAsTooltip() ) );

      } else if (compound->isLinkable()) { // compound link
         QByteArray anchor = compound->anchor();

         if (compound->definitionType() == Definition::TypeFile) {
            name = g_token->name;

         } else if (compound->definitionType() == Definition::TypeGroup) {
            name = ((GroupDef *)compound)->groupTitle();
         }

         children.append(new DocLinkedWord(parent, name, compound->getReference(), compound->getOutputFileBase(),
                                           anchor, compound->briefDescriptionAsTooltip() ) );

      } else if (compound->definitionType() == Definition::TypeFile && ((FileDef *)compound)->generateSourceFile()
                ) { 

         // undocumented file that has source code we can link to
         children.append(new
                         DocLinkedWord(parent, g_token->name, compound->getReference(), compound->getSourceFileBase(),
                                       "", compound->briefDescriptionAsTooltip() )
                        );
      } else { // not linkable
         children.append(new DocWord(parent, name));

      }

   } else if (! s_insideHtmlLink && len > 1 && g_token->name.at(len - 1) == ':') {
      // special case, where matching Foo: fails to be an Obj-C reference,
      // but Foo itself might be linkable.
      g_token->name = g_token->name.left(len - 1);

      handleLinkedWord(parent, children);
      children.append(new DocWord(parent, ":"));

   } else if (! s_insideHtmlLink && (cd = getClass(g_token->name + "-p"))) {
      // special case 2, where the token name is not a class, but could
      // be a Obj-C protocol

      children.append(new DocLinkedWord(parent, name, cd->getReference(), cd->getOutputFileBase(), 
                                    cd->anchor(), cd->briefDescriptionAsTooltip()));

   } else { // normal non-linkable word

      if (g_token->name.left(1) == "#" || g_token->name.left(2) == "::") {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "explicit link request to '%s' could not be resolved", qPrint(name));
         children.append(new DocWord(parent, g_token->name));

      } else {
         children.append(new DocWord(parent, name));
      }
   }
}

static void handleParameterType(DocNode *parent, QList<DocNode *> &children, const QByteArray &paramTypes)
{
   QByteArray name = g_token->name;
   int p = 0, i;
   QByteArray type;

   while ((i = paramTypes.indexOf('|', p)) != -1) {
      g_token->name = paramTypes.mid(p, i - p);
      handleLinkedWord(parent, children);
      p = i + 1;
   }

   g_token->name = paramTypes.mid(p);
   handleLinkedWord(parent, children);
   g_token->name = name;
}

static DocInternalRef *handleInternalRef(DocNode *parent)
{
   //printf("CMD_INTERNALREF\n");
   int tok = doctokenizerYYlex();
   QByteArray tokenName = g_token->name;

   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command", qPrint(tokenName));
      return 0;
   }
   doctokenizerYYsetStateInternalRef();
   tok = doctokenizerYYlex(); // get the reference id
   if (tok != TK_WORD && tok != TK_LNKWORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(tokenName));
      return 0;
   }
   return new DocInternalRef(parent, g_token->name);
}

static DocAnchor *handleAnchor(DocNode *parent)
{
   int tok = doctokenizerYYlex();

   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                     qPrint(g_token->name));
      return 0;
   }

   doctokenizerYYsetStateAnchor();
   tok = doctokenizerYYlex();
   if (tok == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment block while parsing the "
                     "argument of command %s", qPrint(g_token->name));
      return 0;

   } else if (tok != TK_WORD && tok != TK_LNKWORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(g_token->name));
      return 0;
   }

   doctokenizerYYsetStatePara();
   return new DocAnchor(parent, g_token->name, false);
}


/* Helper function that deals with the most common tokens allowed in
 * title like sections.
 * @param parent     Parent node, owner of the children list passed as
 *                   the third argument.
 * @param tok        The token to process.
 * @param children   The list of child nodes to which the node representing
 *                   the token can be added.
 * @param handleWord Indicates if word token should be processed
 * @retval true      The token was handled.
 * @retval false     The token was not handled.
 */
static bool defaultHandleToken(DocNode *parent, int tok, QList<DocNode *> &children, bool handleWord)
{
   DBG(("token %s at %d", tokToString(tok), doctokenizerYYlineno));

   if (tok == TK_WORD || tok == TK_LNKWORD || tok == TK_SYMBOL || tok == TK_URL || tok == TK_COMMAND || tok == TK_HTMLTAG) {
      DBG((" name=%s", qPrint(g_token->name)));
   }

   DBG(("\n"));

   while (true) {
      QByteArray tokenName = g_token->name;

      switch (tok) {
         case TK_COMMAND:

            switch (Mappers::cmdMapper->map(tokenName)) {

               case CMD_BSLASH:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_BSlash));
                  break;
               case CMD_AT:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_At));
                  break;
               case CMD_LESS:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Less));
                  break;
               case CMD_GREATER:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Greater));
                  break;
               case CMD_AMP:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Amp));
                  break;
               case CMD_DOLLAR:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Dollar));
                  break;
               case CMD_HASH:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Hash));
                  break;
               case CMD_DCOLON:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_DoubleColon));
                  break;
               case CMD_PERCENT:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Percent));
                  break;
               case CMD_NDASH:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Minus));
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Minus));
                  break;

               case CMD_MDASH:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Minus));
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Minus));
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Minus));
                  break;

               case CMD_QUOTE:
                  children.append(new DocSymbol(parent, DocSymbol::Sym_Quot));
                  break;

               case CMD_EMPHASIS: {
                  children.append(new DocStyleChange(parent, s_nodeStack.count(), DocStyleChange::Italic, true));
                  tok = handleStyleArgument(parent, children, tokenName);

                  children.append(new DocStyleChange(parent, s_nodeStack.count(), DocStyleChange::Italic, false));

                  if (tok != TK_WORD) {
                     children.append(new DocWhiteSpace(parent, " "));
                  }

                  if (tok == TK_NEWPARA) {
                     if (insidePRE(parent) || !children.isEmpty()) {
                        children.append(new DocWhiteSpace(parent, g_token->chars));
                     }
                     break;

                  } else if (tok == TK_WORD || tok == TK_HTMLTAG) {
                     DBG(("CMD_EMPHASIS: reparsing command %s\n", qPrint(g_token->name)));
                     continue;
                  }
               }

               break;

               case CMD_BOLD: {
                  children.append(new DocStyleChange(parent, s_nodeStack.count(), DocStyleChange::Bold, true));
                  tok = handleStyleArgument(parent, children, tokenName);
                  children.append(new DocStyleChange(parent, s_nodeStack.count(), DocStyleChange::Bold, false));

                  if (tok != TK_WORD) {
                     children.append(new DocWhiteSpace(parent, " "));
                  }

                  if (tok == TK_NEWPARA) {
                   if (insidePRE(parent) || !children.isEmpty()) {
                        children.append(new DocWhiteSpace(parent, g_token->chars));
                     }
                     break;                    

                  } else if (tok == TK_WORD || tok == TK_HTMLTAG) {
                     DBG(("CMD_BOLD: reparsing command %s\n", qPrint(g_token->name)));
                     continue;
                  }
               }
               break;

               case CMD_CODE: {
                  children.append(new DocStyleChange(parent, s_nodeStack.count(), DocStyleChange::Code, true));
                  tok = handleStyleArgument(parent, children, tokenName);
                  children.append(new DocStyleChange(parent, s_nodeStack.count(), DocStyleChange::Code, false));

                  if (tok != TK_WORD) {
                     children.append(new DocWhiteSpace(parent, " "));
                  }

                  if (tok == TK_NEWPARA) {
                     if (insidePRE(parent) || !children.isEmpty()) {
                        children.append(new DocWhiteSpace(parent, g_token->chars));
                     }
                     break;                     

                  } else if (tok == TK_WORD || tok == TK_HTMLTAG) {
                     DBG(("CMD_CODE: reparsing command %s\n", qPrint(g_token->name)));
                     continue;
                  }
               }
               break;

               case CMD_HTMLONLY: {
                  doctokenizerYYsetStateHtmlOnly();
                  tok = doctokenizerYYlex();
                  children.append(new DocVerbatim(parent, s_context, g_token->verb, DocVerbatim::HtmlOnly,
                                  s_isExample, s_exampleName, g_token->name == "block"));

                  if (tok == 0) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "htmlonly section ended without end marker");
                  }
                  doctokenizerYYsetStatePara();
               }
               break;

               case CMD_MANONLY: {
                  doctokenizerYYsetStateManOnly();
                  tok = doctokenizerYYlex();
                  children.append(new DocVerbatim(parent, s_context, g_token->verb, DocVerbatim::ManOnly, s_isExample, s_exampleName));

                  if (tok == 0) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "manonly section ended without end marker");
                  }

                  doctokenizerYYsetStatePara();
               }
               break;

               case CMD_RTFONLY: {
                  doctokenizerYYsetStateRtfOnly();
                  tok = doctokenizerYYlex();
                  children.append(new DocVerbatim(parent, s_context, g_token->verb, DocVerbatim::RtfOnly, s_isExample, s_exampleName));

                  if (tok == 0) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "rtfonly section ended without end marker");
                  }
                  doctokenizerYYsetStatePara();
               }
               break;

               case CMD_LATEXONLY: {
                  doctokenizerYYsetStateLatexOnly();
                  tok = doctokenizerYYlex();
                  children.append(new DocVerbatim(parent, s_context, g_token->verb, DocVerbatim::LatexOnly, s_isExample, s_exampleName));
                  if (tok == 0) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "latexonly section ended without end marker", doctokenizerYYlineno);
                  }
                  doctokenizerYYsetStatePara();
               }
               break;
               case CMD_XMLONLY: {
                  doctokenizerYYsetStateXmlOnly();
                  tok = doctokenizerYYlex();
                  children.append(new DocVerbatim(parent, s_context, g_token->verb, DocVerbatim::XmlOnly, s_isExample, s_exampleName));
                  if (tok == 0) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "xmlonly section ended without end marker", doctokenizerYYlineno);
                  }
                  doctokenizerYYsetStatePara();
               }
               break;
               case CMD_DBONLY: {
                  doctokenizerYYsetStateDbOnly();
                  tok = doctokenizerYYlex();
                  children.append(new DocVerbatim(parent, s_context, g_token->verb, DocVerbatim::DocbookOnly, s_isExample, s_exampleName));
                  if (tok == 0) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "docbookonly section ended without end marker", doctokenizerYYlineno);
                  }
                  doctokenizerYYsetStatePara();
               }
               break;
               case CMD_FORMULA: {
                  DocFormula *form = new DocFormula(parent, g_token->id);
                  children.append(form);
               }
               break;
               case CMD_ANCHOR: {
                  DocAnchor *anchor = handleAnchor(parent);
                  if (anchor) {
                     children.append(anchor);
                  }
               }
               break;
               case CMD_INTERNALREF: {
                  DocInternalRef *ref = handleInternalRef(parent);
                  if (ref) {
                     children.append(ref);
                     ref->parse();
                  }
                  doctokenizerYYsetStatePara();
               }
               break;
               case CMD_SETSCOPE: {
                  QByteArray scope;
                  doctokenizerYYsetStateSetScope();
                  doctokenizerYYlex();
                  scope = g_token->name;
                  s_context = scope;
                  //printf("Found scope='%s'\n",scope.data());
                  doctokenizerYYsetStatePara();
               }
               break;
               default:
                  return false;
            }
            break;

         case TK_HTMLTAG: {
            switch (Mappers::htmlTagMapper->map(tokenName)) {
               case HTML_DIV:
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "found <div> tag in heading\n");
                  break;
               case HTML_PRE:
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "found <pre> tag in heading\n");
                  break;
               case HTML_BOLD:
                  if (!g_token->endTag) {
                     handleStyleEnter(parent, children, DocStyleChange::Bold, &g_token->attribs);
                  } else {
                     handleStyleLeave(parent, children, DocStyleChange::Bold, tokenName);
                  }
                  break;
               case HTML_CODE:
               case XML_C:
                  if (!g_token->endTag) {
                     handleStyleEnter(parent, children, DocStyleChange::Code, &g_token->attribs);
                  } else {
                     handleStyleLeave(parent, children, DocStyleChange::Code, tokenName);
                  }
                  break;
               case HTML_EMPHASIS:
                  if (!g_token->endTag) {
                     handleStyleEnter(parent, children, DocStyleChange::Italic, &g_token->attribs);
                  } else {
                     handleStyleLeave(parent, children, DocStyleChange::Italic, tokenName);
                  }
                  break;
               case HTML_SUB:
                  if (!g_token->endTag) {
                     handleStyleEnter(parent, children, DocStyleChange::Subscript, &g_token->attribs);
                  } else {
                     handleStyleLeave(parent, children, DocStyleChange::Subscript, tokenName);
                  }
                  break;
               case HTML_SUP:
                  if (!g_token->endTag) {
                     handleStyleEnter(parent, children, DocStyleChange::Superscript, &g_token->attribs);
                  } else {
                     handleStyleLeave(parent, children, DocStyleChange::Superscript, tokenName);
                  }
                  break;
               case HTML_CENTER:
                  if (!g_token->endTag) {
                     handleStyleEnter(parent, children, DocStyleChange::Center, &g_token->attribs);
                  } else {
                     handleStyleLeave(parent, children, DocStyleChange::Center, tokenName);
                  }
                  break;
               case HTML_SMALL:
                  if (!g_token->endTag) {
                     handleStyleEnter(parent, children, DocStyleChange::Small, &g_token->attribs);
                  } else {
                     handleStyleLeave(parent, children, DocStyleChange::Small, tokenName);
                  }
                  break;
               default:
                  return false;
                  break;
            }
         }
         break;

         case TK_SYMBOL: {
            DocSymbol::SymType s = DocSymbol::decodeSymbol(tokenName);
            if (s != DocSymbol::Sym_Unknown) {
               children.append(new DocSymbol(parent, s));
            } else {
               return false;
            }
         }
         break;

         case TK_WHITESPACE:
         case TK_NEWPARA:         
            if (insidePRE(parent) || !children.isEmpty()) {
               children.append(new DocWhiteSpace(parent, g_token->chars));
            }
            break;

         case TK_LNKWORD:
            if (handleWord) {
               handleLinkedWord(parent, children);
            } else {
               return false;
            }
            break;

         case TK_WORD:
            if (handleWord) {
               children.append(new DocWord(parent, g_token->name));
            } else {
               return false;
            }
            break;

         case TK_URL:
            if (s_insideHtmlLink) {
               children.append(new DocWord(parent, g_token->name));
            } else {
               children.append(new DocURL(parent, g_token->name, g_token->isEMailAddr));
            }
            break;

         default:
            return false;
      }

      break;
   } 

   return true;
}

static void handleImg(DocNode *parent, QList<DocNode *> &children, const HtmlAttribList &tagHtmlAttribs)
{
   bool found = false;
   int index = 0;
 
   for (auto opt : tagHtmlAttribs) {
     
      if (opt.name == "src" && ! opt.value.isEmpty()) {
         // copy attributes
         HtmlAttribList attrList = tagHtmlAttribs;

         // and remove the src attribute
         attrList.removeAt(index);

         DocImage *img = new DocImage(parent, attrList, opt.value, DocImage::Html, opt.value);
         children.append(img);
         found = true;
      }

      ++index;
   }

   if (! found) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "IMG tag does not have a SRC attribute\n");
   }
}

DocSymbol::SymType DocSymbol::decodeSymbol(const QByteArray &symName)
{
   DBG(("decodeSymbol(%s)\n", qPrint(symName)));
   return HtmlEntityMapper::instance()->name2sym(symName);
}
static int internalValidatingParseDoc(DocNode *parent, QList<DocNode *> &children, const QByteArray &doc)
{
   int retval = RetVal_OK;

   if (doc.isEmpty()) {
      return retval;
   }

   doctokenizerYYinit(doc, s_fileName);

   // first parse any number of paragraphs
   bool isFirst = true;
   DocPara *lastPar = 0;

   if (!children.isEmpty() && children.last()->kind() == DocNode::Kind_Para) {
      // last child item was a paragraph
      lastPar = (DocPara *)children.last();
      isFirst = false;
   }

   do {
      DocPara *par = new DocPara(parent);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }

      retval = par->parse();

      if (!par->isEmpty()) {
         children.append(par);
         if (lastPar) {
            lastPar->markLast(false);
         }

         lastPar = par;
      } else {
         delete par;
      }

   } while (retval == TK_NEWPARA);


   if (lastPar) {
      lastPar->markLast();
   }

   return retval;
}

static void readTextFileByName(const QByteArray &file, QByteArray &text)
{
   QStringList &examplePathList = Config_getList("EXAMPLE_PATH");
   
   for (auto s : examplePathList) {
      QString absFileName = s + portable_pathSeparator() + file;
      QFileInfo fi(absFileName);

      if (fi.exists()) {
         text = fileToString(qPrintable(absFileName), Config_getBool("FILTER_SOURCE_FILES"));
         return;
      }  
   }

   // as a fallback we also look in the exampleNameDict
   bool ambig;
   FileDef *fd;

   if ((fd = findFileDef(Doxygen::exampleNameDict, file, ambig))) {
      text = fileToString(fd->getFilePath(), Config_getBool("FILTER_SOURCE_FILES"));

   } else if (ambig) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "included file name %s is ambiguous"
                     "Possible candidates:\n%s", qPrint(file), qPrint(showFileDefMatches(Doxygen::exampleNameDict, file)));

   } else {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "included file %s is not found. "
                     "Check your EXAMPLE_PATH", qPrint(file));
   }
}

DocWord::DocWord(DocNode *parent, const QByteArray &word) 
   : m_word(word)
{
   m_parent = parent;
   
   if (Doxygen::searchIndex && !s_searchUrl.isEmpty()) {
      Doxygen::searchIndex->addWord(word, false);
   }
}

DocLinkedWord::DocLinkedWord(DocNode *parent, const QByteArray &word, const QByteArray &ref, const QByteArray &file,
                             const QByteArray &anchor, const QByteArray &tooltip) 
   : m_word(word), m_ref(ref), m_file(file), m_relPath(s_relPath), m_anchor(anchor), m_tooltip(tooltip)
{
   m_parent = parent;
   
   if (Doxygen::searchIndex && !s_searchUrl.isEmpty()) {
      Doxygen::searchIndex->addWord(word, false);
   }
}

DocAnchor::DocAnchor(DocNode *parent, const QByteArray &id, bool newAnchor)
{
   m_parent = parent;

   if (id.isEmpty()) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "Empty anchor label");
   }

   if (newAnchor) { // found <a name="label">
      m_anchor = id;

   } else if (id.left(CiteConsts::anchorPrefix.length()) == CiteConsts::anchorPrefix) {
      CiteInfo *cite = Doxygen::citeDict->find(id.mid(CiteConsts::anchorPrefix.length()));

      if (cite) {
         m_file = convertNameToFile(CiteConsts::fileName, false, true).toUtf8();
         m_anchor = id;

      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Invalid cite anchor id `%s'", qPrint(id));
         m_anchor = "invalid";
         m_file = "invalid";
      }

   } else { 
      // found \anchor label
      QSharedPointer<SectionInfo> sec = Doxygen::sectionDict->find(id);

      if (sec) {
         //printf("Found anchor %s\n",id.data());
         m_file   = sec->fileName;
         m_anchor = sec->label;

         if (s_sectionDict && s_sectionDict->find(id) == 0) {           
            s_sectionDict->insert(id, sec);
         }

      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Invalid anchor id `%s'", qPrint(id));
         m_anchor = "invalid";
         m_file = "invalid";
      }
   }
}

DocVerbatim::DocVerbatim(DocNode *parent, const QByteArray &context, const QByteArray &text, Type t, bool isExample,
                         const QByteArray &exampleFile, bool isBlock, const QByteArray &lang) 
   : m_context(context), m_text(text), m_type(t), m_isExample(isExample), m_exampleFile(exampleFile),
     m_relPath(s_relPath), m_lang(lang), m_isBlock(isBlock)
{
   m_parent = parent;
}

void DocInclude::parse()
{
   DBG(("DocInclude::parse(file=%s,text=%s)\n", qPrint(m_file), qPrint(m_text)));

   switch (m_type) {
      case IncWithLines:     
      case Include:
      case DontInclude:
         readTextFileByName(m_file, m_text);
         s_includeFileText   = m_text;
         s_includeFileOffset = 0;
         s_includeFileLength = m_text.length();        
         break;

      case VerbInclude:  
      case HtmlInclude:
         readTextFileByName(m_file, m_text);
         break;

      case LatexInclude:
         readTextFileByName(m_file, m_text);
         break;

      case Snippet:
         readTextFileByName(m_file, m_text);
         // check here for the existence of the blockId inside the file, so we
         // only generate the warning once
         int count;

         if (! m_blockId.isEmpty() && (count = m_text.count(m_blockId.data())) != 2) {

            warn_doc_error(s_fileName, doctokenizerYYlineno, "block marked with %s for \\snippet should appear twice in file %s, found it %d times\n",
                           m_blockId.data(), m_file.data(), count);
         }
         break;
   }
}

void DocIncOperator::parse()
{
   const char *p = s_includeFileText;
   uint l = s_includeFileLength;
   uint o = s_includeFileOffset;

   DBG(("DocIncOperator::parse() text=%s off=%d len=%d\n", qPrint(p), o, l));

   uint so = o, bo;
   bool nonEmpty = false;

   switch (type()) {
      case Line:
         while (o < l) {
            char c = p[o];
            if (c == '\n') {
               if (nonEmpty) {
                  break;   // we have a pattern to match
               }
               so = o + 1; // no pattern, skip empty line
            } else if (!isspace((uchar)c)) { // no white space char
               nonEmpty = true;
            }
            o++;
         }

         if (s_includeFileText.mid(so, o - so).indexOf(m_pattern) != -1) {
            m_text = s_includeFileText.mid(so, o - so);
            DBG(("DocIncOperator::parse() Line: %s\n", qPrint(m_text)));
         }
         s_includeFileOffset = qMin(l, o + 1); // set pointer to start of new line
         break;

      case SkipLine:
         while (o < l) {
            so = o;
            while (o < l) {
               char c = p[o];
               if (c == '\n') {
                  if (nonEmpty) {
                     break;   // we have a pattern to match
                  }
                  so = o + 1; // no pattern, skip empty line
               } else if (!isspace((uchar)c)) { // no white space char
                  nonEmpty = true;
               }
               o++;
            }
            if (s_includeFileText.mid(so, o - so).indexOf(m_pattern) != -1) {
               m_text = s_includeFileText.mid(so, o - so);
               DBG(("DocIncOperator::parse() SkipLine: %s\n", qPrint(m_text)));
               break;
            }
            o++; // skip new line
         }
         s_includeFileOffset = qMin(l, o + 1); // set pointer to start of new line
         break;
      case Skip:
         while (o < l) {
            so = o;
            while (o < l) {
               char c = p[o];
               if (c == '\n') {
                  if (nonEmpty) {
                     break;   // we have a pattern to match
                  }
                  so = o + 1; // no pattern, skip empty line
               } else if (!isspace((uchar)c)) { // no white space char
                  nonEmpty = true;
               }
               o++;
            }
            if (s_includeFileText.mid(so, o - so).indexOf(m_pattern) != -1) {
               break;
            }
            o++; // skip new line
         }
         s_includeFileOffset = so; // set pointer to start of new line
         break;
      case Until:
         bo = o;
         while (o < l) {
            so = o;
            while (o < l) {
               char c = p[o];
               if (c == '\n') {
                  if (nonEmpty) {
                     break;   // we have a pattern to match
                  }
                  so = o + 1; // no pattern, skip empty line
               } else if (!isspace((uchar)c)) { // no white space char
                  nonEmpty = true;
               }
               o++;
            }
            if (s_includeFileText.mid(so, o - so).indexOf(m_pattern) != -1) {
               m_text = s_includeFileText.mid(bo, o - bo);
               DBG(("DocIncOperator::parse() Until: %s\n", qPrint(m_text)));
               break;
            }
            o++; // skip new line
         }
         s_includeFileOffset = qMin(l, o + 1); // set pointer to start of new line
         break;
   }
}

void DocCopy::parse(QList<DocNode *> &children)
{
   QByteArray doc, brief;
   Definition *def;

   if (findDocsForMemberOrCompound(m_link, &doc, &brief, &def)) {

      if (s_copyStack.indexOf(def) == -1) { // definition not parsed earlier
         bool  hasParamCommand  = s_hasParamCommand;
         bool  hasReturnCommand = s_hasReturnCommand;

         QHash<QString, void *>  paramsFound  = s_paramsFound;
 
         docParserPushContext(false);
         s_scope = def;

         if (def->definitionType() == Definition::TypeMember && def->getOuterScope()) {
            if (def->getOuterScope() != Doxygen::globalScope) {
               s_context = def->getOuterScope()->name();
            }
         } else if (def != Doxygen::globalScope) {
            s_context = def->name();
         }

         s_styleStack.clear();
         s_nodeStack.clear();
         s_paramsFound.clear();
         s_copyStack.append(def);

         // make sure the descriptions end with a newline, so the parser will correctly
         // handle them in all cases.

         if (m_copyBrief) {
            brief += '\n';
            internalValidatingParseDoc(m_parent, children, brief);

            hasParamCommand  = hasParamCommand  || s_hasParamCommand;
            hasReturnCommand = hasReturnCommand || s_hasReturnCommand;
                                 
            for (auto iter = s_paramsFound.begin(); iter != s_paramsFound.end(); ++iter) { 
               paramsFound.insert(iter.key(), iter.value());
            }
         }

         if (m_copyDetails) {
            doc += '\n';
            internalValidatingParseDoc(m_parent, children, doc);

            hasParamCommand  = hasParamCommand  || s_hasParamCommand;
            hasReturnCommand = hasReturnCommand || s_hasReturnCommand;            

            for (auto iter = s_paramsFound.begin(); iter != s_paramsFound.end(); ++iter) { 
               paramsFound.insert(iter.key(), iter.value());
            }
         }

         s_copyStack.removeOne(def);
         assert(s_styleStack.isEmpty());
         assert(s_nodeStack.isEmpty());

         docParserPopContext(true);

         s_hasParamCommand  = hasParamCommand;
         s_hasReturnCommand = hasReturnCommand;
         s_paramsFound      = paramsFound;

      } else { // oops, recursion
         warn_doc_error(s_fileName, doctokenizerYYlineno, "recursive call chain of \\copydoc commands detected at %d\n",
                        doctokenizerYYlineno);
      }

   } else {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "target %s of \\copydoc command not found", qPrint(m_link));
   }
}

DocXRefItem::DocXRefItem(DocNode *parent, int id, const char *key) :
   m_id(id), m_key(key), m_relPath(s_relPath)
{
   m_parent = parent;
}

bool DocXRefItem::parse()
{
   QByteArray listName;
   auto refList = Doxygen::xrefLists->find(m_key);

   if (refList != Doxygen::xrefLists->end() && 
                  ((m_key != "todo"       || Config_getBool("GENERATE_TODOLIST")) &&
                   (m_key != "test"       || Config_getBool("GENERATE_TESTLIST")) &&
                   (m_key != "bug"        || Config_getBool("GENERATE_BUGLIST"))  &&
                   (m_key != "deprecated" || Config_getBool("GENERATE_DEPRECATEDLIST"))) ) {

      // either not a built-in list or the list is enabled
      RefItem *item = refList->getRefItem(m_id);

      assert(item != 0);

      if (item) {
         if (s_memberDef && s_memberDef->name().at(0) == '@') {
            m_file   = "@";  // can't cross reference anonymous enum
            m_anchor = "@";

         } else {
            m_file   = convertNameToFile(refList->listName(), false, true).toUtf8();
            m_anchor = item->listAnchor;
         }

         m_title  = refList->sectionTitle();
       
         if (!item->text.isEmpty()) {
            docParserPushContext();
            internalValidatingParseDoc(this, m_children, item->text);
            docParserPopContext();
         }
      }

      return true;
   }
   return false;
}

DocFormula::DocFormula(DocNode *parent, int id) 
   : m_relPath(s_relPath)
{
   m_parent = parent;

   QString formCmd;
   formCmd = QString("\\form#%1").arg(id);

   auto formula = Doxygen::formulaNameDict->find(formCmd);

   if (formula != Doxygen::formulaNameDict->end()) {
      m_id = formula->getId();   
      m_name = QString("form_%1").arg(m_id).toUtf8();

      m_text = formula->getFormulaText();

   } else { // wrong \form#<n> command
      warn_doc_error(s_fileName, doctokenizerYYlineno, "Incorrect formula id %d", id);
      m_id = -1;

   }
}

//int DocLanguage::parse()
//{
//  int retval;
//  DBG(("DocLanguage::parse() start\n"));
//  s_nodeStack.push(this);
//
//  // parse one or more paragraphs
//  bool isFirst=true;
//  DocPara *par=0;
//  do
//  {
//    par = new DocPara(this);
//    if (isFirst) { par->markFirst(); isFirst=false; }
//    m_children.append(par);
//    retval=par->parse();
//  }
//  while (retval==TK_NEWPARA);
//  if (par) par->markLast();
//
//  DBG(("DocLanguage::parse() end\n"));
//  DocNode *n = s_nodeStack.pop();
//  assert(n==this);
//  return retval;
//}

void DocSecRefItem::parse()
{
   DBG(("DocSecRefItem::parse() start\n"));
   s_nodeStack.push(this);

   doctokenizerYYsetStateTitle();
   int tok;

   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a \\refitem",
                              qPrint(g_token->name));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }
   doctokenizerYYsetStatePara();
   handlePendingStyleCommands(this, m_children);

   QSharedPointer<SectionInfo> sec;

   if (!m_target.isEmpty()) {
      sec = Doxygen::sectionDict->find(m_target);

      if (sec) {
         m_file   = sec->fileName;
         m_anchor = sec->label;

         if (s_sectionDict && s_sectionDict->find(m_target) == 0) {
            s_sectionDict->insert(m_target, sec);
         }

      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "reference to unknown section %s", qPrint(m_target));
      }

   } else {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "reference to empty target");
   }

   DBG(("DocSecRefItem::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
}

void DocSecRefList::parse()
{
   DBG(("DocSecRefList::parse() start\n"));
   s_nodeStack.push(this);

   int tok = doctokenizerYYlex();
   // skip white space
   while (tok == TK_WHITESPACE || tok == TK_NEWPARA) {
      tok = doctokenizerYYlex();
   }
   // handle items
   while (tok) {
      if (tok == TK_COMMAND) {
         switch (Mappers::cmdMapper->map(g_token->name)) {
            case CMD_SECREFITEM: {
               int tok = doctokenizerYYlex();
               if (tok != TK_WHITESPACE) {
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after \\refitem command");
                  break;
               }
               tok = doctokenizerYYlex();
               if (tok != TK_WORD && tok != TK_LNKWORD) {
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of \\refitem",
                                 tokToString(tok));
                  break;
               }

               DocSecRefItem *item = new DocSecRefItem(this, g_token->name);
               m_children.append(item);
               item->parse();
            }
            break;
            case CMD_ENDSECREFLIST:
               goto endsecreflist;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a \\secreflist",
                              qPrint(g_token->name));
               goto endsecreflist;
         }
      } else if (tok == TK_WHITESPACE) {
         // ignore whitespace
      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s inside section reference list",
                        tokToString(tok));
         goto endsecreflist;
      }
      tok = doctokenizerYYlex();
   }

endsecreflist:
   DBG(("DocSecRefList::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
}

//---------------------------------------------------------------------------

DocInternalRef::DocInternalRef(DocNode *parent, const QByteArray &ref)
   : m_relPath(s_relPath)
{
   m_parent = parent;
   int i = ref.indexOf('#');

   if (i != -1) {
      m_anchor = ref.right(ref.length() - i - 1);
      m_file   = ref.left(i);

   } else {
      m_file = ref;
   }
}

void DocInternalRef::parse()
{
   s_nodeStack.push(this);
   DBG(("DocInternalRef::parse() start\n"));

   int tok;
   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a \\ref",
                              qPrint(g_token->name));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }

   handlePendingStyleCommands(this, m_children);
   DBG(("DocInternalRef::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
}

//---------------------------------------------------------------------------

DocRef::DocRef(DocNode *parent, const QByteArray &target, const QByteArray &context) :
   m_refToSection(false), m_refToAnchor(false), m_isSubPage(false)
{
   m_parent = parent;
   Definition  *compound = 0;

   QByteArray  anchor;   
   assert(!target.isEmpty());

   SrcLangExt lang = getLanguageFromFileName(target);

   m_relPath = s_relPath;

   QSharedPointer<SectionInfo> sec = Doxygen::sectionDict->find(target);

   if (sec == 0 && lang == SrcLangExt_Markdown) { // lookup as markdown file
      sec = Doxygen::sectionDict->find(markdownFileNameToId(target));
   }

   if (sec) { // ref to section or anchor      
      QSharedPointer<PageDef> pd;

      if (sec->type == SectionInfo::Page) {
         pd = Doxygen::pageSDict->find(target);
      }

      m_text = sec->title;

      if (m_text.isEmpty()) {
         m_text = sec->label;
      }

      m_ref          = sec->ref;
      m_file         = stripKnownExtensions(sec->fileName);
      m_refToAnchor  = sec->type == SectionInfo::Anchor;
      m_refToSection = sec->type != SectionInfo::Anchor;
      m_isSubPage    = pd && pd->hasParentPage();

      if (sec->type != SectionInfo::Page || m_isSubPage) {
         m_anchor = sec->label;
      }
      //printf("m_text=%s,m_ref=%s,m_file=%s,m_refToAnchor=%d type=%d\n",
      //    m_text.data(),m_ref.data(),m_file.data(),m_refToAnchor,sec->type);
      return;

   } else if (resolveLink(context, target, true, &compound, anchor)) {
      bool isFile = compound ?
                    (compound->definitionType() == Definition::TypeFile ||
                     compound->definitionType() == Definition::TypePage ? true : false) :
                    false;
      m_text = linkToText(compound ? compound->getLanguage() : SrcLangExt_Unknown, target, isFile);
      m_anchor = anchor;
      if (compound && compound->isLinkable()) { // ref to compound
         if (anchor.isEmpty() &&                                  /* compound link */
               compound->definitionType() == Definition::TypeGroup && /* is group */
               ((GroupDef *)compound)->groupTitle()                 /* with title */
            ) {
            m_text = ((GroupDef *)compound)->groupTitle(); // use group's title as link
         } else if (compound->definitionType() == Definition::TypeMember &&
                    ((MemberDef *)compound)->isObjCMethod()) {
            // Objective C Method
            MemberDef *member = (MemberDef *)compound;
            bool localLink = s_memberDef ? member->getClassDef() == s_memberDef->getClassDef() : false;
            m_text = member->objCMethodName(localLink, s_inSeeBlock);
         }

         m_file = compound->getOutputFileBase();
         m_ref  = compound->getReference();
         //printf("isFile=%d compound=%s (%d)\n",isFile,compound->name().data(),
         //    compound->definitionType());
         return;
      } else if (compound && compound->definitionType() == Definition::TypeFile &&
                 ((FileDef *)compound)->generateSourceFile()
                ) { // undocumented file that has source code we can link to
         m_file = compound->getSourceFileBase();
         m_ref  = compound->getReference();
         return;
      }
   }
   m_text = target;
   warn_doc_error(s_fileName, doctokenizerYYlineno, "unable to resolve reference to `%s' for \\ref command",
                  qPrint(target));
}

static void flattenParagraphs(DocNode *root, QList<DocNode *> &children)
{
   QList<DocNode*> newChildren;

   for (auto dn : children) {
      if (dn->kind() == DocNode::Kind_Para) {
         DocPara *para = (DocPara *)dn;
         QList<DocNode *> &paraChildren = para->children();        
     
         for (auto item : paraChildren) {
            newChildren.append(item); // add them to new node
         }
      }
   }

   children.clear();
  
   for (auto item : newChildren) {
      children.append(item);
      item->setParent(root);
   }
}

void DocRef::parse()
{
   s_nodeStack.push(this);
   DBG(("DocRef::parse() start\n"));

   int tok;
   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a \\ref",
                              qPrint(g_token->name));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            case TK_HTMLTAG:
               break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }

   if (m_children.isEmpty() && !m_text.isEmpty()) {
      s_insideHtmlLink = true;
      docParserPushContext();
      internalValidatingParseDoc(this, m_children, m_text);
      docParserPopContext();
      s_insideHtmlLink = false;
      flattenParagraphs(this, m_children);
   }

   handlePendingStyleCommands(this, m_children);

   DocNode *n = s_nodeStack.pop();
   assert(n == this);
}

DocCite::DocCite(DocNode *parent, const QByteArray &target, const QByteArray &) //context)
{
   static uint numBibFiles = Config_getList("CITE_BIB_FILES").count();
   m_parent = parent;
   QByteArray anchor;

   assert(!target.isEmpty());

   m_relPath = s_relPath;
   CiteInfo *cite = Doxygen::citeDict->find(target);

   if (numBibFiles > 0 && cite && !cite->text.isEmpty()) {
      // ref to citation

      m_text         = cite->text;
      m_ref          = cite->ref;
      m_anchor       = CiteConsts::anchorPrefix + cite->label;
      m_file         = convertNameToFile(CiteConsts::fileName, false, true).toUtf8();
     
      return;
   }

   m_text = target;
   warn_doc_error(s_fileName, doctokenizerYYlineno, "unable to resolve reference to `%s' for \\cite command", qPrint(target));
}

DocLink::DocLink(DocNode *parent, const QByteArray &target)
{
   m_parent = parent;
   Definition *compound = 0;
   QByteArray anchor;
   m_refText = target;
   m_relPath = s_relPath;
   if (!m_refText.isEmpty() && m_refText.at(0) == '#') {
      m_refText = m_refText.right(m_refText.length() - 1);
   }
   if (resolveLink(s_context, stripKnownExtensions(target), s_inSeeBlock,
                   &compound, anchor)) {
      m_anchor = anchor;
      if (compound && compound->isLinkable()) {
         m_file = compound->getOutputFileBase();
         m_ref  = compound->getReference();
      } else if (compound && compound->definitionType() == Definition::TypeFile &&
                 ((FileDef *)compound)->generateSourceFile()
                ) { // undocumented file that has source code we can link to
         m_file = compound->getSourceFileBase();
         m_ref  = compound->getReference();
      }
      return;
   }

   // bogus link target
   warn_doc_error(s_fileName, doctokenizerYYlineno, "unable to resolve link to `%s' for \\link command",
                  qPrint(target));
}


QByteArray DocLink::parse(bool isJavaLink, bool isXmlLink)
{
   QByteArray result;
   s_nodeStack.push(this);
   DBG(("DocLink::parse() start\n"));

   int tok;
   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children, false)) {
         switch (tok) {
            case TK_COMMAND:
               switch (Mappers::cmdMapper->map(g_token->name)) {
                  case CMD_ENDLINK:
                     if (isJavaLink) {
                        warn_doc_error(s_fileName, doctokenizerYYlineno, "{@link.. ended with @endlink command");
                     }
                     goto endlink;
                  default:
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a \\link",
                                    qPrint(g_token->name));
                     break;
               }
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            case TK_HTMLTAG:
               if (g_token->name != "see" || !isXmlLink) {
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected xml/html command %s found",
                                 qPrint(g_token->name));
               }
               goto endlink;
            case TK_LNKWORD:
            case TK_WORD:
               if (isJavaLink) { // special case to detect closing }
                  QByteArray w = g_token->name;
                  int p;
                  if (w == "}") {
                     goto endlink;

                  } else if ((p = w.indexOf('}')) != -1) {
                     uint l = w.length();
                     m_children.append(new DocWord(this, w.left(p)));

                     if ((uint)p < l - 1) { // something left after the } (for instance a .)
                        result = w.right(l - p - 1);
                     }

                     goto endlink;
                  }
               }
               m_children.append(new DocWord(this, g_token->name));
               break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }
   if (tok == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected end of comment while inside"
                     " link command\n");
   }
endlink:

   if (m_children.isEmpty()) { // no link text
      m_children.append(new DocWord(this, m_refText));
   }

   handlePendingStyleCommands(this, m_children);
   DBG(("DocLink::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return result;
}

DocDotFile::DocDotFile(DocNode *parent, const QByteArray &name, const QByteArray &context) :
   m_name(name), m_relPath(s_relPath), m_context(context)
{
   m_parent = parent;
}

void DocDotFile::parse()
{
   s_nodeStack.push(this);
   DBG(("DocDotFile::parse() start\n"));

   doctokenizerYYsetStateTitle();
   int tok;
   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a \\dotfile",
                              qPrint(g_token->name));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }
   tok = doctokenizerYYlex();
   while (tok == TK_WORD) { // there are values following the title
      if (g_token->name == "width") {
         m_width = g_token->chars;
      } else if (g_token->name == "height") {
         m_height = g_token->chars;
      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unknown option %s after image title",
                        qPrint(g_token->name));
      }
      tok = doctokenizerYYlex();
   }
   assert(tok == 0);

   doctokenizerYYsetStatePara();
   handlePendingStyleCommands(this, m_children);

   bool ambig;
   FileDef *fd = findFileDef(Doxygen::dotFileNameDict, m_name, ambig);

   if (fd == 0 && m_name.right(4) != ".dot") { // try with .dot extension as well
      fd = findFileDef(Doxygen::dotFileNameDict, m_name + ".dot", ambig);
   }

   if (fd) {
      m_file = fd->getFilePath();

   } else if (ambig) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "included dot file name %s is ambiguous.\n"
                     "Possible candidates:\n%s", qPrint(m_name),
                     qPrint(showFileDefMatches(Doxygen::exampleNameDict, m_name))
                    );
   } else {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "included dot file %s is not found "
                     "in any of the paths specified via DOTFILE_DIRS!", qPrint(m_name));
   }

   DBG(("DocDotFile::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
}

DocMscFile::DocMscFile(DocNode *parent, const QByteArray &name, const QByteArray &context) :
   m_name(name), m_relPath(s_relPath), m_context(context)
{
   m_parent = parent;
}

void DocMscFile::parse()
{
   s_nodeStack.push(this);
   DBG(("DocMscFile::parse() start\n"));

   doctokenizerYYsetStateTitle();
   int tok;
   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a \\mscfile",
                              qPrint(g_token->name));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }
   tok = doctokenizerYYlex();
   while (tok == TK_WORD) { // there are values following the title
      if (g_token->name == "width") {
         m_width = g_token->chars;
      } else if (g_token->name == "height") {
         m_height = g_token->chars;
      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unknown option %s after image title",
                        qPrint(g_token->name));
      }
      tok = doctokenizerYYlex();
   }
   assert(tok == 0);
   doctokenizerYYsetStatePara();
   handlePendingStyleCommands(this, m_children);

   bool ambig;
   FileDef *fd = findFileDef(Doxygen::mscFileNameDict, m_name, ambig);
   if (fd == 0 && m_name.right(4) != ".msc") { // try with .msc extension as well
      fd = findFileDef(Doxygen::mscFileNameDict, m_name + ".msc", ambig);
   }
   if (fd) {
      m_file = fd->getFilePath();
   } else if (ambig) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "included msc file name %s is ambiguous.\n"
                     "Possible candidates:\n%s", qPrint(m_name),
                     qPrint(showFileDefMatches(Doxygen::exampleNameDict, m_name)));

   } else {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "included msc file %s is not found "
                     "in any of the paths specified via MSCFILE_DIRS!", qPrint(m_name));
   }

   DBG(("DocMscFile::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
}

//---------------------------------------------------------------------------

DocDiaFile::DocDiaFile(DocNode *parent, const QByteArray &name, const QByteArray &context) :
   m_name(name), m_relPath(s_relPath), m_context(context)
{
   m_parent = parent;
}

void DocDiaFile::parse()
{
   s_nodeStack.push(this);
   DBG(("DocDiaFile::parse() start\n"));

   doctokenizerYYsetStateTitle();
   int tok;
   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a \\diafile",
                              qPrint(g_token->name));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }

   tok = doctokenizerYYlex();

   while (tok == TK_WORD) { // there are values following the title
      if (g_token->name == "width") {
         m_width = g_token->chars;
      } else if (g_token->name == "height") {
         m_height = g_token->chars;
      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unknown option %s after image title",
                        qPrint(g_token->name));
      }
      tok = doctokenizerYYlex();
   }

   assert(tok == 0);
   doctokenizerYYsetStatePara();
   handlePendingStyleCommands(this, m_children);

   bool ambig;
   FileDef *fd = findFileDef(Doxygen::diaFileNameDict, m_name, ambig);

   if (fd == 0 && m_name.right(4) != ".dia") { // try with .dia extension as well
      fd = findFileDef(Doxygen::diaFileNameDict, m_name + ".dia", ambig);
   }

   if (fd) {
      m_file = fd->getFilePath();

   } else if (ambig) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "included dia file name %s is ambiguous.\n"
                     "Possible candidates:\n%s", qPrint(m_name),
                     qPrint(showFileDefMatches(Doxygen::exampleNameDict, m_name)));

   } else {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "included dia file %s is not found "
                     "in any of the paths specified via DIAFILE_DIRS!", qPrint(m_name));
   }

   DBG(("DocDiaFile::parse() end\n"));
   DocNode *n = s_nodeStack.pop();

   assert(n == this);
}

DocImage::DocImage(DocNode *parent, const HtmlAttribList &attribs, const QByteArray &name, Type t, const QByteArray &url) 
   : m_attribs(attribs), m_name(name), m_type(t), m_relPath(s_relPath), m_url(url)
{
   m_parent = parent;
}

void DocImage::parse()
{
   s_nodeStack.push(this);
   DBG(("DocImage::parse() start\n"));

   // parse title
   doctokenizerYYsetStateTitle();
   int tok;

   while ((tok = doctokenizerYYlex())) {
      if (tok == TK_WORD && (g_token->name == "width=" || g_token->name == "height=")) {
         // special case: no title, but we do have a size indicator
         doctokenizerYYsetStateTitleAttrValue();
         
         g_token->name = g_token->name.left(g_token->name.length() - 1);
         break;
      }

      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a \\image",
                              qPrint(g_token->name));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }
   // parse size attributes
   tok = doctokenizerYYlex();
   while (tok == TK_WORD) { // there are values following the title
      if (g_token->name == "width") {
         m_width = g_token->chars;
      } else if (g_token->name == "height") {
         m_height = g_token->chars;
      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unknown option %s after image title",
                        qPrint(g_token->name));
      }
      tok = doctokenizerYYlex();
   }
   doctokenizerYYsetStatePara();

   handlePendingStyleCommands(this, m_children);
   DBG(("DocImage::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
}


//---------------------------------------------------------------------------

int DocHtmlHeader::parse()
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);
   DBG(("DocHtmlHeader::parse() start\n"));

   int tok;
   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a <h%d> tag",
                              qPrint(g_token->name), m_level);
               break;
            case TK_HTMLTAG: {
               int tagId = Mappers::htmlTagMapper->map(g_token->name);
               if (tagId == HTML_H1 && g_token->endTag) { // found </h1> tag
                  if (m_level != 1) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "<h%d> ended with </h1>",
                                    m_level);
                  }
                  goto endheader;
               } else if (tagId == HTML_H2 && g_token->endTag) { // found </h2> tag
                  if (m_level != 2) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "<h%d> ended with </h2>",
                                    m_level);
                  }
                  goto endheader;
               } else if (tagId == HTML_H3 && g_token->endTag) { // found </h3> tag
                  if (m_level != 3) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "<h%d> ended with </h3>",
                                    m_level);
                  }
                  goto endheader;
               } else if (tagId == HTML_H4 && g_token->endTag) { // found </h4> tag
                  if (m_level != 4) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "<h%d> ended with </h4>",
                                    m_level);
                  }
                  goto endheader;
               } else if (tagId == HTML_H5 && g_token->endTag) { // found </h5> tag
                  if (m_level != 5) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "<h%d> ended with </h5>",
                                    m_level);
                  }
                  goto endheader;
               } else if (tagId == HTML_H6 && g_token->endTag) { // found </h6> tag
                  if (m_level != 6) {
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "<h%d> ended with </h6>",
                                    m_level);
                  }
                  goto endheader;
               } else if (tagId == HTML_A) {
                  if (!g_token->endTag) {
                     handleAHref(this, m_children, g_token->attribs);
                  }
               } else if (tagId == HTML_BR) {
                  DocLineBreak *lb = new DocLineBreak(this);
                  m_children.append(lb);
               } else {
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected html tag <%s%s> found within <h%d> context",
                                 g_token->endTag ? "/" : "", qPrint(g_token->name), m_level);
               }

            }
            break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }
   if (tok == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected end of comment while inside"
                     " <h%d> tag\n", m_level);
   }
endheader:
   handlePendingStyleCommands(this, m_children);
   DBG(("DocHtmlHeader::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

//---------------------------------------------------------------------------

int DocHRef::parse()
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);
   DBG(("DocHRef::parse() start\n"));

   int tok;
   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a <a>..</a> block",
                              qPrint(g_token->name));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            case TK_HTMLTAG:

            {
               int tagId = Mappers::htmlTagMapper->map(g_token->name);
               if (tagId == HTML_A && g_token->endTag) { // found </a> tag
                  goto endhref;
               } else {
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected html tag <%s%s> found within <a href=...> context",
                                 g_token->endTag ? "/" : "", qPrint(g_token->name), doctokenizerYYlineno);
               }
            }
            break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok), doctokenizerYYlineno);
               break;
         }
      }
   }
   if (tok == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected end of comment while inside"
                     " <a href=...> tag", doctokenizerYYlineno);
   }
endhref:
   handlePendingStyleCommands(this, m_children);
   DBG(("DocHRef::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

//---------------------------------------------------------------------------

int DocInternal::parse(int level)
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);
   DBG(("DocInternal::parse() start\n"));

   // first parse any number of paragraphs
   bool isFirst = true;
   DocPara *lastPar = 0;
   do {
      DocPara *par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }
      retval = par->parse();
      if (!par->isEmpty()) {
         m_children.append(par);
         lastPar = par;
      } else {
         delete par;
      }
      if (retval == TK_LISTITEM) {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Invalid list item found", doctokenizerYYlineno);
      }
   } while (retval != 0 &&
            retval != RetVal_Section &&
            retval != RetVal_Subsection &&
            retval != RetVal_Subsubsection &&
            retval != RetVal_Paragraph &&
            retval != RetVal_EndInternal
           );
   if (lastPar) {
      lastPar->markLast();
   }

   // then parse any number of level-n sections
   while ((level == 1 && retval == RetVal_Section) ||
          (level == 2 && retval == RetVal_Subsection) ||
          (level == 3 && retval == RetVal_Subsubsection) ||
          (level == 4 && retval == RetVal_Paragraph)
         ) {
      DocSection *s = new DocSection(this,
                                     qMin(level + Doxygen::subpageNestingLevel, 5), g_token->sectionId);
      m_children.append(s);
      retval = s->parse();
   }

   if (retval == RetVal_Internal) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "\\internal command found inside internal section");
   }

   DBG(("DocInternal::parse() end: retval=%x\n", retval));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

//---------------------------------------------------------------------------

int DocIndexEntry::parse()
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);
   DBG(("DocIndexEntry::parse() start\n"));
   int tok = doctokenizerYYlex();
   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after \\addindex command");
      goto endindexentry;
   }
   doctokenizerYYsetStateTitle();
   m_entry = "";
   while ((tok = doctokenizerYYlex())) {
      switch (tok) {
         case TK_WHITESPACE:
            m_entry += " ";
            break;
         case TK_WORD:
         case TK_LNKWORD:
            m_entry += g_token->name;
            break;
         case TK_SYMBOL: {
            DocSymbol::SymType s = DocSymbol::decodeSymbol(g_token->name);
            switch (s) {
               case DocSymbol::Sym_BSlash:
                  m_entry += '\\';
                  break;
               case DocSymbol::Sym_At:
                  m_entry += '@';
                  break;
               case DocSymbol::Sym_Less:
                  m_entry += '<';
                  break;
               case DocSymbol::Sym_Greater:
                  m_entry += '>';
                  break;
               case DocSymbol::Sym_Amp:
                  m_entry += '&';
                  break;
               case DocSymbol::Sym_Dollar:
                  m_entry += '$';
                  break;
               case DocSymbol::Sym_Hash:
                  m_entry += '#';
                  break;
               case DocSymbol::Sym_Percent:
                  m_entry += '%';
                  break;
               case DocSymbol::Sym_apos:
                  m_entry += '\'';
                  break;
               case DocSymbol::Sym_Quot:
                  m_entry += '"';
                  break;
               case DocSymbol::Sym_lsquo:
                  m_entry += '`';
                  break;
               case DocSymbol::Sym_rsquo:
                  m_entry += '\'';
                  break;
               case DocSymbol::Sym_ldquo:
                  m_entry += "``";
                  break;
               case DocSymbol::Sym_rdquo:
                  m_entry += "''";
                  break;
               case DocSymbol::Sym_ndash:
                  m_entry += "--";
                  break;
               case DocSymbol::Sym_mdash:
                  m_entry += "---";
                  break;
               default:
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected symbol found as argument of \\addindex");
                  break;
            }
         }
         break;
         case TK_COMMAND:
            switch (Mappers::cmdMapper->map(g_token->name)) {
               case CMD_BSLASH:
                  m_entry += '\\';
                  break;
               case CMD_AT:
                  m_entry += '@';
                  break;
               case CMD_LESS:
                  m_entry += '<';
                  break;
               case CMD_GREATER:
                  m_entry += '>';
                  break;
               case CMD_AMP:
                  m_entry += '&';
                  break;
               case CMD_DOLLAR:
                  m_entry += '$';
                  break;
               case CMD_HASH:
                  m_entry += '#';
                  break;
               case CMD_DCOLON:
                  m_entry += "::";
                  break;
               case CMD_PERCENT:
                  m_entry += '%';
                  break;
               case CMD_NDASH:
                  m_entry += "--";
                  break;
               case CMD_MDASH:
                  m_entry += "---";
                  break;
               case CMD_QUOTE:
                  m_entry += '"';
                  break;
               default:
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected command %s found as argument of \\addindex",
                                 qPrint(g_token->name));
                  break;
            }
            break;
         default:
            warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                           tokToString(tok));
            break;
      }
   }
   doctokenizerYYsetStatePara();
   m_entry = m_entry.trimmed();

endindexentry:
   DBG(("DocIndexEntry::parse() end retval=%x\n", retval));
   DocNode *n = s_nodeStack.pop();

   assert(n == this);

   return retval;
}

//---------------------------------------------------------------------------

int DocHtmlCaption::parse()
{
   int retval = 0;
   s_nodeStack.push(this);
   DBG(("DocHtmlCaption::parse() start\n"));
   int tok;
   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a <caption> tag",
                              qPrint(g_token->name));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            case TK_HTMLTAG: {
               int tagId = Mappers::htmlTagMapper->map(g_token->name);
               if (tagId == HTML_CAPTION && g_token->endTag) { // found </caption> tag
                  retval = RetVal_OK;
                  goto endcaption;
               } else {
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected html tag <%s%s> found within <caption> context",
                                 g_token->endTag ? "/" : "", qPrint(g_token->name));
               }
            }
            break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }
   if (tok == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected end of comment while inside"
                     " <caption> tag", doctokenizerYYlineno);
   }
endcaption:
   handlePendingStyleCommands(this, m_children);
   DBG(("DocHtmlCaption::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

//---------------------------------------------------------------------------

int DocHtmlCell::parse()
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);
   DBG(("DocHtmlCell::parse() start\n"));

   // parse one or more paragraphs
   bool isFirst = true;
   DocPara *par = 0;
   do {
      par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }
      m_children.append(par);
      retval = par->parse();
      if (retval == TK_HTMLTAG) {
         int tagId = Mappers::htmlTagMapper->map(g_token->name);
         if (tagId == HTML_TD && g_token->endTag) { // found </dt> tag
            retval = TK_NEWPARA; // ignore the tag
         } else if (tagId == HTML_TH && g_token->endTag) { // found </th> tag
            retval = TK_NEWPARA; // ignore the tag
         }
      }
   } while (retval == TK_NEWPARA);
   if (par) {
      par->markLast();
   }

   DBG(("DocHtmlCell::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

int DocHtmlCell::parseXml()
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);
   DBG(("DocHtmlCell::parseXml() start\n"));

   // parse one or more paragraphs
   bool isFirst = true;
   DocPara *par = 0;
   do {
      par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }
      m_children.append(par);
      retval = par->parse();
      if (retval == TK_HTMLTAG) {
         int tagId = Mappers::htmlTagMapper->map(g_token->name);
         if (tagId == XML_ITEM && g_token->endTag) { // found </item> tag
            retval = TK_NEWPARA; // ignore the tag
         } else if (tagId == XML_DESCRIPTION && g_token->endTag) { // found </description> tag
            retval = TK_NEWPARA; // ignore the tag
         }
      }
   } while (retval == TK_NEWPARA);
   if (par) {
      par->markLast();
   }

   DBG(("DocHtmlCell::parseXml() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

int DocHtmlCell::rowSpan() const
{
   int retval = 0;
   HtmlAttribList attrs = attribs();
   uint i;

   for (i = 0; i < attrs.count(); ++i) {
      if (attrs.at(i).name.toLower() == "rowspan") {
         retval = attrs.at(i).value.toInt();
         break;
      }
   }
   return retval;
}

int DocHtmlCell::colSpan() const
{
   int retval = 1;
   HtmlAttribList attrs = attribs();
   uint i;

   for (i = 0; i < attrs.count(); ++i) {
      if (attrs.at(i).name.toLower() == "colspan") {
         retval = qMax(1, attrs.at(i).value.toInt());
         break;
      }
   }
   return retval;
}

DocHtmlCell::Alignment DocHtmlCell::alignment() const
{
   HtmlAttribList attrs = attribs();
   uint i;

   for (i = 0; i < attrs.count(); ++i) {

      if (attrs.at(i).name.toLower() == "align") {
         if (attrs.at(i).value.toLower() == "center") {
            return Center;

         } else if (attrs.at(i).value.toLower() == "right") {
            return Right;

         } else {
            return Left;
         }
      }
   }
   return Left;
}

int DocHtmlRow::parse()
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);

   DBG(("DocHtmlRow::parse() start\n"));

   bool isHeading = false;
   bool isFirst = true;
   DocHtmlCell *cell = 0;

   // get next token
   int tok = doctokenizerYYlex();
   // skip whitespace
   while (tok == TK_WHITESPACE || tok == TK_NEWPARA) {
      tok = doctokenizerYYlex();
   }
   // should find a html tag now
   if (tok == TK_HTMLTAG) {
      int tagId = Mappers::htmlTagMapper->map(g_token->name);
      if (tagId == HTML_TD && !g_token->endTag) { // found <td> tag
      } else if (tagId == HTML_TH && !g_token->endTag) { // found <th> tag
         isHeading = true;
      } else { // found some other tag
         warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <td> or <th> tag but "
                        "found <%s> instead!", qPrint(g_token->name));
         doctokenizerYYpushBackHtmlTag(g_token->name);
         goto endrow;
      }
   } else if (tok == 0) { // premature end of comment
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment while looking"
                     " for a html description title");
      goto endrow;
   } else { // token other than html token
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <td> or <th> tag but found %s token instead!",
                     tokToString(tok));
      goto endrow;
   }

   // parse one or more cells
   do {
      cell = new DocHtmlCell(this, g_token->attribs, isHeading);
      cell->markFirst(isFirst);
      isFirst = false;
      m_children.append(cell);
      retval = cell->parse();
      isHeading = retval == RetVal_TableHCell;
   } while (retval == RetVal_TableCell || retval == RetVal_TableHCell);
   if (cell) {
      cell->markLast(true);
   }

endrow:
   DBG(("DocHtmlRow::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

int DocHtmlRow::parseXml(bool isHeading)
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);
   DBG(("DocHtmlRow::parseXml() start\n"));

   bool isFirst = true;
   DocHtmlCell *cell = 0;

   // get next token
   int tok = doctokenizerYYlex();
   // skip whitespace
   while (tok == TK_WHITESPACE || tok == TK_NEWPARA) {
      tok = doctokenizerYYlex();
   }
   // should find a html tag now
   if (tok == TK_HTMLTAG) {
      int tagId = Mappers::htmlTagMapper->map(g_token->name);
      if (tagId == XML_TERM && !g_token->endTag) { // found <term> tag
      } else if (tagId == XML_DESCRIPTION && !g_token->endTag) { // found <description> tag
      } else { // found some other tag
         warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <term> or <description> tag but "
                        "found <%s> instead!", qPrint(g_token->name));
         doctokenizerYYpushBackHtmlTag(g_token->name);
         goto endrow;
      }
   } else if (tok == 0) { // premature end of comment
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment while looking"
                     " for a html description title");
      goto endrow;
   } else { // token other than html token
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <td> or <th> tag but found %s token instead!",
                     tokToString(tok));
      goto endrow;
   }

   do {
      cell = new DocHtmlCell(this, g_token->attribs, isHeading);
      cell->markFirst(isFirst);
      isFirst = false;
      m_children.append(cell);
      retval = cell->parseXml();
   } while (retval == RetVal_TableCell || retval == RetVal_TableHCell);
   if (cell) {
      cell->markLast(true);
   }

endrow:
   DBG(("DocHtmlRow::parseXml() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

//---------------------------------------------------------------------------

int DocHtmlTable::parse()
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);
   DBG(("DocHtmlTable::parse() start\n"));

getrow:
   // get next token
   int tok = doctokenizerYYlex();
   // skip whitespace
   while (tok == TK_WHITESPACE || tok == TK_NEWPARA) {
      tok = doctokenizerYYlex();
   }
   // should find a html tag now
   if (tok == TK_HTMLTAG) {
      int tagId = Mappers::htmlTagMapper->map(g_token->name);
      if (tagId == HTML_TR && !g_token->endTag) { // found <tr> tag
         // no caption, just rows
         retval = RetVal_TableRow;
      } else if (tagId == HTML_CAPTION && !g_token->endTag) { // found <caption> tag
         if (m_caption) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "table already has a caption, found another one");
         } else {
            m_caption = new DocHtmlCaption(this, g_token->attribs);
            retval = m_caption->parse();

            if (retval == RetVal_OK) { // caption was parsed ok
               goto getrow;
            }
         }
      } else { // found wrong token
         warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <tr> or <caption> tag but "
                        "found <%s%s> instead!", g_token->endTag ? "/" : "", qPrint(g_token->name));
      }
   } else if (tok == 0) { // premature end of comment
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment while looking"
                     " for a <tr> or <caption> tag");
   } else { // token other than html token
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <tr> tag but found %s token instead!",
                     tokToString(tok));
   }

   // parse one or more rows
   while (retval == RetVal_TableRow) {
      DocHtmlRow *tr = new DocHtmlRow(this, g_token->attribs);
      m_children.append(tr);
      retval = tr->parse();
   }

   computeTableGrid();

   DBG(("DocHtmlTable::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval == RetVal_EndTable ? RetVal_OK : retval;
}

int DocHtmlTable::parseXml()
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);
   DBG(("DocHtmlTable::parseXml() start\n"));

   // get next token
   int tok = doctokenizerYYlex();
   // skip whitespace
   while (tok == TK_WHITESPACE || tok == TK_NEWPARA) {
      tok = doctokenizerYYlex();
   }
   // should find a html tag now
   int tagId = 0;
   bool isHeader = false;
   if (tok == TK_HTMLTAG) {
      tagId = Mappers::htmlTagMapper->map(g_token->name);
      if (tagId == XML_ITEM && !g_token->endTag) { // found <item> tag
         retval = RetVal_TableRow;
      }
      if (tagId == XML_LISTHEADER && !g_token->endTag) { // found <listheader> tag
         retval = RetVal_TableRow;
         isHeader = true;
      }
   }

   // parse one or more rows
   while (retval == RetVal_TableRow) {
      DocHtmlRow *tr = new DocHtmlRow(this, g_token->attribs);
      m_children.append(tr);
      retval = tr->parseXml(isHeader);
      isHeader = false;
   }

   computeTableGrid();

   DBG(("DocHtmlTable::parseXml() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval == RetVal_EndTable ? RetVal_OK : retval;
}

/** Helper class to compute the grid for an HTML style table */
struct ActiveRowSpan {
   ActiveRowSpan(int rows, int col) : rowsLeft(rows), column(col) {}
   int rowsLeft;
   int column;
};

/** List of ActiveRowSpan classes. */
typedef QList<ActiveRowSpan> RowSpanList;

/** determines the location of all cells in a grid, resolving row and
    column spans. For each the total number of visible cells is computed,
    and the total number of visible columns over all rows is stored.
 */
void DocHtmlTable::computeTableGrid()
{
   //printf("computeTableGrid()\n");
   RowSpanList rowSpans;
   
   int maxCols = 0;
   int rowIdx = 1;

   for (auto rowNode : children()) {
      int colIdx = 1;
      int cells = 0;

      if (rowNode->kind() == DocNode::Kind_HtmlRow) {
         uint i;
         DocHtmlRow *row = (DocHtmlRow *)rowNode;         

         for (auto cellNode : row->children()) {

            if (cellNode->kind() == DocNode::Kind_HtmlCell) {
               DocHtmlCell *cell = (DocHtmlCell *)cellNode;
               int rs = cell->rowSpan();
               int cs = cell->colSpan();

               for (i = 0; i < rowSpans.count(); i++) {
                  if (rowSpans.at(i).rowsLeft > 0 && rowSpans.at(i).column == colIdx) {
                     colIdx = rowSpans.at(i).column + 1;
                     cells++;
                  }
               }

               if (rs > 0) {
                  rowSpans.append(ActiveRowSpan(rs, colIdx));
               }
               
               cell->setRowIndex(rowIdx);
               cell->setColumnIndex(colIdx);
               colIdx += cs;
               cells++;
            }
         }

         for (i = 0; i < rowSpans.count(); i++) {
            if (rowSpans[i].rowsLeft > 0) {
               rowSpans[i].rowsLeft--;
            }
         }

         row->setVisibleCells(cells);
         row->setRowIndex(rowIdx);
         rowIdx++;
      }

      if (colIdx - 1 > maxCols) {
         maxCols = colIdx - 1;
      }
   }

   m_numCols = maxCols;
}

void DocHtmlTable::accept(DocVisitor *v)
{
   v->visitPre(this);

   // for HTML output we put the caption first
   if (m_caption && v->id() == DocVisitor_Html) {
      m_caption->accept(v);
   } 

   for (auto n : m_children) {
      n->accept(v);
   }

   // for other output formats we put the caption last
   if (m_caption && v->id() != DocVisitor_Html) {
      m_caption->accept(v);
   }

   v->visitPost(this);
}

int DocHtmlDescTitle::parse()
{
   int retval = 0;
   s_nodeStack.push(this);

   DBG(("DocHtmlDescTitle::parse() start\n"));

   int tok;
   while ((tok = doctokenizerYYlex())) {

      if (!defaultHandleToken(this, tok, m_children)) {

         switch (tok) {

            case TK_COMMAND: {
               QByteArray cmdName = g_token->name;
               bool isJavaLink = false;

               switch (Mappers::cmdMapper->map(cmdName)) {
                  case CMD_REF: {
                     int tok = doctokenizerYYlex();
                     if (tok != TK_WHITESPACE) {
                        warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                                       qPrint(g_token->name));
                     } else {
                        doctokenizerYYsetStateRef();
                        tok = doctokenizerYYlex(); // get the reference id
                        if (tok != TK_WORD) {
                           warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                                          tokToString(tok), qPrint(cmdName));
                        } else {
                           DocRef *ref = new DocRef(this, g_token->name, s_context);
                           m_children.append(ref);
                           ref->parse();
                        }
                        doctokenizerYYsetStatePara();
                     }
                  }
                  break;
                  case CMD_JAVALINK:
                     isJavaLink = true;
                  // fall through
                  case CMD_LINK: {
                     int tok = doctokenizerYYlex();
                     if (tok != TK_WHITESPACE) {
                        warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                                       qPrint(cmdName));
                     } else {
                        doctokenizerYYsetStateLink();
                        tok = doctokenizerYYlex();
                        if (tok != TK_WORD) {
                           warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                                          tokToString(tok), qPrint(cmdName));
                        } else {
                           doctokenizerYYsetStatePara();
                           DocLink *lnk = new DocLink(this, g_token->name);
                           m_children.append(lnk);
                           QByteArray leftOver = lnk->parse(isJavaLink);
                           if (!leftOver.isEmpty()) {
                              m_children.append(new DocWord(this, leftOver));
                           }
                        }
                     }
                  }

                  break;
                  default:
                     warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a <dt> tag",
                                    qPrint(g_token->name));
               }
            }
            break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            case TK_HTMLTAG: {
               int tagId = Mappers::htmlTagMapper->map(g_token->name);
               if (tagId == HTML_DD && !g_token->endTag) { // found <dd> tag
                  retval = RetVal_DescData;
                  goto endtitle;
               } else if (tagId == HTML_DT && g_token->endTag) {
                  // ignore </dt> tag.
               } else if (tagId == HTML_DT) {
                  // missing <dt> tag.
                  retval = RetVal_DescTitle;
                  goto endtitle;
               } else if (tagId == HTML_DL && g_token->endTag) {
                  retval = RetVal_EndDesc;
                  goto endtitle;
               } else if (tagId == HTML_A) {
                  if (!g_token->endTag) {
                     handleAHref(this, m_children, g_token->attribs);
                  }
               } else {
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected html tag <%s%s> found within <dt> context",
                                 g_token->endTag ? "/" : "", qPrint(g_token->name));
               }
            }
            break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }
   if (tok == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected end of comment while inside"
                     " <dt> tag");
   }
endtitle:
   handlePendingStyleCommands(this, m_children);
   DBG(("DocHtmlDescTitle::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

//---------------------------------------------------------------------------

int DocHtmlDescData::parse()
{
   m_attribs = g_token->attribs;
   int retval = 0;
   s_nodeStack.push(this);
   DBG(("DocHtmlDescData::parse() start\n"));

   bool isFirst = true;
   DocPara *par = 0;
   do {
      par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }
      m_children.append(par);
      retval = par->parse();
   } while (retval == TK_NEWPARA);
   if (par) {
      par->markLast();
   }

   DBG(("DocHtmlDescData::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

//---------------------------------------------------------------------------

int DocHtmlDescList::parse()
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);
   DBG(("DocHtmlDescList::parse() start\n"));

   // get next token
   int tok = doctokenizerYYlex();
   // skip whitespace
   while (tok == TK_WHITESPACE || tok == TK_NEWPARA) {
      tok = doctokenizerYYlex();
   }
   // should find a html tag now
   if (tok == TK_HTMLTAG) {
      int tagId = Mappers::htmlTagMapper->map(g_token->name);
      if (tagId == HTML_DT && !g_token->endTag) { // found <dt> tag
         // continue
      } else { // found some other tag
         warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <dt> tag but "
                        "found <%s> instead!", qPrint(g_token->name));
         doctokenizerYYpushBackHtmlTag(g_token->name);
         goto enddesclist;
      }
   } else if (tok == 0) { // premature end of comment
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment while looking"
                     " for a html description title");
      goto enddesclist;
   } else { // token other than html token
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <dt> tag but found %s token instead!",
                     tokToString(tok));
      goto enddesclist;
   }

   do {
      DocHtmlDescTitle *dt = new DocHtmlDescTitle(this, g_token->attribs);
      m_children.append(dt);
      DocHtmlDescData *dd = new DocHtmlDescData(this);
      m_children.append(dd);
      retval = dt->parse();
      if (retval == RetVal_DescData) {
         retval = dd->parse();
      } else if (retval != RetVal_DescTitle) {
         // error
         break;
      }
   } while (retval == RetVal_DescTitle);

   if (retval == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment while inside <dl> block");
   }

enddesclist:

   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   DBG(("DocHtmlDescList::parse() end\n"));
   return retval == RetVal_EndDesc ? RetVal_OK : retval;
}

//---------------------------------------------------------------------------

int DocHtmlListItem::parse()
{
   DBG(("DocHtmlListItem::parse() start\n"));
   int retval = 0;
   s_nodeStack.push(this);

   // parse one or more paragraphs
   bool isFirst = true;
   DocPara *par = 0;
   do {
      par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }
      m_children.append(par);
      retval = par->parse();
   } while (retval == TK_NEWPARA);
   if (par) {
      par->markLast();
   }

   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   DBG(("DocHtmlListItem::parse() end retval=%x\n", retval));
   return retval;
}

int DocHtmlListItem::parseXml()
{
   DBG(("DocHtmlListItem::parseXml() start\n"));
   int retval = 0;
   s_nodeStack.push(this);

   // parse one or more paragraphs
   bool isFirst = true;
   DocPara *par = 0;
   do {
      par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }
      m_children.append(par);
      retval = par->parse();
      if (retval == 0) {
         break;
      }

      //printf("new item: retval=%x g_token->name=%s g_token->endTag=%d\n",
      //    retval,qPrint(g_token->name),g_token->endTag);
      if (retval == RetVal_ListItem) {
         break;
      }
   } while (retval != RetVal_CloseXml);

   if (par) {
      par->markLast();
   }

   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   DBG(("DocHtmlListItem::parseXml() end retval=%x\n", retval));
   return retval;
}

//---------------------------------------------------------------------------

int DocHtmlList::parse()
{
   DBG(("DocHtmlList::parse() start\n"));
   int retval = RetVal_OK;
   int num = 1;
   s_nodeStack.push(this);

   // get next token
   int tok = doctokenizerYYlex();
   // skip whitespace and paragraph breaks
   while (tok == TK_WHITESPACE || tok == TK_NEWPARA) {
      tok = doctokenizerYYlex();
   }
   // should find a html tag now
   if (tok == TK_HTMLTAG) {
      int tagId = Mappers::htmlTagMapper->map(g_token->name);
      if (tagId == HTML_LI && !g_token->endTag) { // found <li> tag
         // ok, we can go on.
      } else if (((m_type == Unordered && tagId == HTML_UL) ||
                  (m_type == Ordered   && tagId == HTML_OL)
                 ) && g_token->endTag
                ) { // found empty list
         // add dummy item to obtain valid HTML
         m_children.append(new DocHtmlListItem(this, HtmlAttribList(), 1));
         warn_doc_error(s_fileName, doctokenizerYYlineno, "empty list!");
         retval = RetVal_EndList;
         goto endlist;
      } else { // found some other tag
         // add dummy item to obtain valid HTML
         m_children.append(new DocHtmlListItem(this, HtmlAttribList(), 1));
         warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <li> tag but "
                        "found <%s%s> instead!", g_token->endTag ? "/" : "", qPrint(g_token->name));
         doctokenizerYYpushBackHtmlTag(g_token->name);
         goto endlist;
      }
   } else if (tok == 0) { // premature end of comment
      // add dummy item to obtain valid HTML
      m_children.append(new DocHtmlListItem(this, HtmlAttribList(), 1));
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment while looking"
                     " for a html list item");
      goto endlist;
   } else { // token other than html token
      // add dummy item to obtain valid HTML
      m_children.append(new DocHtmlListItem(this, HtmlAttribList(), 1));
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <li> tag but found %s token instead!",
                     tokToString(tok));
      goto endlist;
   }

   do {
      DocHtmlListItem *li = new DocHtmlListItem(this, g_token->attribs, num++);
      m_children.append(li);
      retval = li->parse();
   } while (retval == RetVal_ListItem);

   if (retval == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment while inside <%cl> block",
                     m_type == Unordered ? 'u' : 'o');
   }

endlist:
   DBG(("DocHtmlList::parse() end retval=%x\n", retval));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval == RetVal_EndList ? RetVal_OK : retval;
}

int DocHtmlList::parseXml()
{
   DBG(("DocHtmlList::parseXml() start\n"));
   int retval = RetVal_OK;
   int num = 1;
   s_nodeStack.push(this);

   // get next token
   int tok = doctokenizerYYlex();
   // skip whitespace and paragraph breaks
   while (tok == TK_WHITESPACE || tok == TK_NEWPARA) {
      tok = doctokenizerYYlex();
   }
   // should find a html tag now
   if (tok == TK_HTMLTAG) {
      int tagId = Mappers::htmlTagMapper->map(g_token->name);
      //printf("g_token->name=%s g_token->endTag=%d\n",qPrint(g_token->name),g_token->endTag);
      if (tagId == XML_ITEM && !g_token->endTag) { // found <item> tag
         // ok, we can go on.
      } else { // found some other tag
         warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <item> tag but "
                        "found <%s> instead!", qPrint(g_token->name));
         doctokenizerYYpushBackHtmlTag(g_token->name);
         goto endlist;
      }
   } else if (tok == 0) { // premature end of comment
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment while looking"
                     " for a html list item");
      goto endlist;
   } else { // token other than html token
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected <item> tag but found %s token instead!",
                     tokToString(tok));
      goto endlist;
   }

   do {
      DocHtmlListItem *li = new DocHtmlListItem(this, g_token->attribs, num++);
      m_children.append(li);
      retval = li->parseXml();
      if (retval == 0) {
         break;
      }
      //printf("retval=%x g_token->name=%s\n",retval,qPrint(g_token->name));
   } while (retval == RetVal_ListItem);

   if (retval == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment while inside <list type=\"%s\"> block",
                     m_type == Unordered ? "bullet" : "number");
   }

endlist:
   DBG(("DocHtmlList::parseXml() end retval=%x\n", retval));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval == RetVal_EndList ||
          (retval == RetVal_CloseXml || g_token->name == "list") ?
          RetVal_OK : retval;
}

//--------------------------------------------------------------------------

int DocHtmlBlockQuote::parse()
{
   DBG(("DocHtmlBlockQuote::parse() start\n"));
   int retval = 0;
   s_nodeStack.push(this);

   // parse one or more paragraphs
   bool isFirst = true;
   DocPara *par = 0;
   do {
      par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }
      m_children.append(par);
      retval = par->parse();
   } while (retval == TK_NEWPARA);
   if (par) {
      par->markLast();
   }

   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   DBG(("DocHtmlBlockQuote::parse() end retval=%x\n", retval));
   return (retval == RetVal_EndBlockQuote) ? RetVal_OK : retval;
}

//---------------------------------------------------------------------------

int DocParBlock::parse()
{
   DBG(("DocParBlock::parse() start\n"));
   int retval = 0;
   s_nodeStack.push(this);

   // parse one or more paragraphs
   bool isFirst = true;
   DocPara *par = 0;
   do {
      par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }
      m_children.append(par);
      retval = par->parse();
   } while (retval == TK_NEWPARA);
   if (par) {
      par->markLast();
   }

   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   DBG(("DocParBlock::parse() end retval=%x\n", retval));
   return (retval == RetVal_EndBlockQuote) ? RetVal_OK : retval;
}

//---------------------------------------------------------------------------

int DocSimpleListItem::parse()
{
   s_nodeStack.push(this);
   int rv = m_paragraph->parse();
   m_paragraph->markFirst();
   m_paragraph->markLast();
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return rv;
}

//--------------------------------------------------------------------------

int DocSimpleList::parse()
{
   s_nodeStack.push(this);
   int rv;
   do {
      DocSimpleListItem *li = new DocSimpleListItem(this);
      m_children.append(li);
      rv = li->parse();
   } while (rv == RetVal_ListItem);
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return (rv != TK_NEWPARA) ? rv : RetVal_OK;
}

//--------------------------------------------------------------------------

DocAutoListItem::DocAutoListItem(DocNode *parent, int indent, int num)
   : m_indent(indent), m_itemNum(num)
{
   m_parent = parent;
}

int DocAutoListItem::parse()
{
   int retval = RetVal_OK;
   s_nodeStack.push(this);

   // first parse any number of paragraphs
   bool isFirst = true;
   DocPara *lastPar = 0;
   do {
      DocPara *par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }
      retval = par->parse();
      if (!par->isEmpty()) {
         m_children.append(par);
         if (lastPar) {
            lastPar->markLast(false);
         }
         lastPar = par;
      } else {
         delete par;
      }
      // next paragraph should be more indented than the - marker to belong
      // to this item
   } while (retval == TK_NEWPARA && g_token->indent > m_indent);
   if (lastPar) {
      lastPar->markLast();
   }

   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   //printf("DocAutoListItem: retval=%d indent=%d\n",retval,g_token->indent);
   return retval;
}

//--------------------------------------------------------------------------

DocAutoList::DocAutoList(DocNode *parent, int indent, bool isEnumList,
                         int depth) :
   m_indent(indent), m_isEnumList(isEnumList),
   m_depth(depth)
{
   m_parent = parent;
}

int DocAutoList::parse()
{
   int retval = RetVal_OK;
   int num = 1;
   s_nodeStack.push(this);
   doctokenizerYYstartAutoList();
   // first item or sub list => create new list
   do {
      if (g_token->id != -1) { // explicitly numbered list
         num = g_token->id; // override num with real number given
      }
      DocAutoListItem *li = new DocAutoListItem(this, m_indent, num++);
      m_children.append(li);
      retval = li->parse();
      //printf("DocAutoList::parse(): retval=0x%x g_token->indent=%d m_indent=%d "
      //       "m_isEnumList=%d g_token->isEnumList=%d g_token->name=%s\n",
      //       retval,g_token->indent,m_indent,m_isEnumList,g_token->isEnumList,
      //       g_token->name.data());
      //printf("num=%d g_token->id=%d\n",num,g_token->id);
   } while (retval == TK_LISTITEM &&            // new list item
            m_indent == g_token->indent &&        // at same indent level
            m_isEnumList == g_token->isEnumList && // of the same kind
            (g_token->id == -1 || g_token->id >= num) // increasing number (or no number)
           );

   doctokenizerYYendAutoList();
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

void DocTitle::parse()
{
   DBG(("DocTitle::parse() start\n"));
   s_nodeStack.push(this);
   doctokenizerYYsetStateTitle();
   int tok;

   while ((tok = doctokenizerYYlex())) {
      if (!defaultHandleToken(this, tok, m_children)) {
         switch (tok) {
            case TK_COMMAND:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal command %s as part of a title section",
                              qPrint(g_token->name));
               break;
            case TK_SYMBOL:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
               break;
            default:
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s",
                              tokToString(tok));
               break;
         }
      }
   }
   doctokenizerYYsetStatePara();
   handlePendingStyleCommands(this, m_children);
   DBG(("DocTitle::parse() end\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
}

void DocTitle::parseFromString(const QByteArray &text)
{
   m_children.append(new DocWord(this, text));
}

DocSimpleSect::DocSimpleSect(DocNode *parent, Type t) :
   m_type(t)
{
   m_parent = parent;
   m_title = 0;
}

DocSimpleSect::~DocSimpleSect()
{
   delete m_title;
}

void DocSimpleSect::accept(DocVisitor *v)
{
   v->visitPre(this);

   if (m_title) {
      m_title->accept(v);
   }
 
   for (auto n : m_children) {
      n->accept(v);
   }
   v->visitPost(this);
}

int DocSimpleSect::parse(bool userTitle, bool needsSeparator)
{
   DBG(("DocSimpleSect::parse() start\n"));
   s_nodeStack.push(this);

   // handle case for user defined title
   if (userTitle) {
      m_title = new DocTitle(this);
      m_title->parse();
   }

   // add new paragraph as child
   DocPara *par = new DocPara(this);
   if (m_children.isEmpty()) {
      par->markFirst();
   } else {
      assert(m_children.last()->kind() == DocNode::Kind_Para);
      ((DocPara *)m_children.last())->markLast(false);
   }

   par->markLast();
   if (needsSeparator) {
      m_children.append(new DocSimpleSectSep(this));
   }
   m_children.append(par);

   // parse the contents of the paragraph
   int retval = par->parse();

   DBG(("DocSimpleSect::parse() end retval=%d\n", retval));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval; // 0==EOF, TK_NEWPARA, TK_LISTITEM, TK_ENDLIST, RetVal_SimpleSec
}

int DocSimpleSect::parseRcs()
{
   DBG(("DocSimpleSect::parseRcs() start\n"));
   s_nodeStack.push(this);

   m_title = new DocTitle(this);
   m_title->parseFromString(g_token->name);

   QByteArray text = g_token->text;
   docParserPushContext(); // this will create a new g_token
   internalValidatingParseDoc(this, m_children, text);
   docParserPopContext(); // this will restore the old g_token

   DBG(("DocSimpleSect::parseRcs()\n"));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);

   return RetVal_OK;
}

int DocSimpleSect::parseXml()
{
   DBG(("DocSimpleSect::parse() start\n"));
   s_nodeStack.push(this);

   int retval = RetVal_OK;
   for (;;) {
      // add new paragraph as child
      DocPara *par = new DocPara(this);

      if (m_children.isEmpty()) {
         par->markFirst();

      } else {
         assert(m_children.last()->kind() == DocNode::Kind_Para);
         ((DocPara *)m_children.last())->markLast(false);
      }

      par->markLast();
      m_children.append(par);

      // parse the contents of the paragraph
      retval = par->parse();
      if (retval == 0) {
         break;
      }
      if (retval == RetVal_CloseXml) {
         retval = RetVal_OK;
         break;
      }
   }

   DBG(("DocSimpleSect::parseXml() end retval=%d\n", retval));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

void DocSimpleSect::appendLinkWord(const QByteArray &word)
{
   DocPara *p;
   if (m_children.isEmpty() || m_children.last()->kind() != DocNode::Kind_Para) {
      p = new DocPara(this);
      m_children.append(p);

   } else {
      p = (DocPara *)m_children.last();

      // Comma-seperate <seealso> links.
      p->injectToken(TK_WORD, ",");
      p->injectToken(TK_WHITESPACE, " ");
   }

   s_inSeeBlock = true;
   p->injectToken(TK_LNKWORD, word);
   s_inSeeBlock = false;
}

QByteArray DocSimpleSect::typeString() const
{
   switch (m_type) {
      case Unknown:
         break;
      case See:
         return "see";
      case Return:
         return "return";
      case Author:     // fall through
      case Authors:
         return "author";
      case Version:
         return "version";
      case Since:
         return "since";
      case Date:
         return "date";
      case Note:
         return "note";
      case Warning:
         return "warning";
      case Pre:
         return "pre";
      case Post:
         return "post";
      case Copyright:
         return "copyright";
      case Invar:
         return "invariant";
      case Remark:
         return "remark";
      case Attention:
         return "attention";
      case User:
         return "user";
      case Rcs:
         return "rcs";
   }
   return "unknown";
}

int DocParamList::parse(const QByteArray &cmdName)
{
   int retval = RetVal_OK;
   DBG(("DocParamList::parse() start\n"));

   s_nodeStack.push(this);
   DocPara *par = 0;
   QByteArray saveCmdName = cmdName;

   int tok = doctokenizerYYlex();
   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                     qPrint(cmdName));
      retval = 0;
      goto endparamlist;
   }
   doctokenizerYYsetStateParam();
   tok = doctokenizerYYlex();

   while (tok == TK_WORD) { /* there is a parameter name */
      if (m_type == DocParamSect::Param) {
         int typeSeparator = g_token->name.indexOf('#'); // explicit type position

         if (typeSeparator != -1) {
            handleParameterType(this, m_paramTypes, g_token->name.left(typeSeparator));
            g_token->name = g_token->name.mid(typeSeparator + 1);
            s_hasParamCommand = true;
            checkArgumentName(g_token->name, true);
            ((DocParamSect *)parent())->m_hasTypeSpecifier = true;

         } else {
            s_hasParamCommand = true;
            checkArgumentName(g_token->name, true);
         }

      } else if (m_type == DocParamSect::RetVal) {
         s_hasReturnCommand = true;
         checkArgumentName(g_token->name, false);
      }

      //m_params.append(g_token->name);
      handleLinkedWord(this, m_params);
      tok = doctokenizerYYlex();
   }

   doctokenizerYYsetStatePara();

   if (tok == 0) { /* premature end of comment block */
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment block while parsing the "
                     "argument of command %s", qPrint(cmdName));
      retval = 0;
      goto endparamlist;
   }
   if (tok != TK_WHITESPACE) { /* premature end of comment block */
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token in comment block while parsing the "
                     "argument of command %s", qPrint(saveCmdName));
      retval = 0;
      goto endparamlist;
   }

   par = new DocPara(this);
   m_paragraphs.append(par);
   retval = par->parse();
   par->markFirst();
   par->markLast();

endparamlist:
   DBG(("DocParamList::parse() end retval=%d\n", retval));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

int DocParamList::parseXml(const QByteArray &paramName)
{
   int retval = RetVal_OK;
   DBG(("DocParamList::parseXml() start\n"));
   s_nodeStack.push(this);

   g_token->name = paramName;
   if (m_type == DocParamSect::Param) {
      s_hasParamCommand = true;
      checkArgumentName(g_token->name, true);
   } else if (m_type == DocParamSect::RetVal) {
      s_hasReturnCommand = true;
      checkArgumentName(g_token->name, false);
   }

   handleLinkedWord(this, m_params);

   do {
      DocPara *par = new DocPara(this);
      retval = par->parse();

      if (par->isEmpty()) // avoid adding an empty paragraph for the whitespace
         // after </para> and before </param>
      {
         delete par;
         break;

      } else { // append the paragraph to the list
         if (m_paragraphs.isEmpty()) {
            par->markFirst();
         } else {
            m_paragraphs.last().markLast(false);
         }

         par->markLast();
         m_paragraphs.append(par);
      }

      if (retval == 0) {
         break;
      }

   } while (retval == RetVal_CloseXml &&
            Mappers::htmlTagMapper->map(g_token->name) != XML_PARAM &&
            Mappers::htmlTagMapper->map(g_token->name) != XML_TYPEPARAM &&
            Mappers::htmlTagMapper->map(g_token->name) != XML_EXCEPTION);


   if (retval == 0) { /* premature end of comment block */
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unterminated param or exception tag");
   } else {
      retval = RetVal_OK;
   }

   DBG(("DocParamList::parse() end retval=%d\n", retval));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);

   return retval;
}

int DocParamSect::parse(const QByteArray &cmdName, bool xmlContext, Direction d)
{
   int retval = RetVal_OK;
   DBG(("DocParamSect::parse() start\n"));
   s_nodeStack.push(this);

   if (d != Unspecified) {
      m_hasInOutSpecifier = true;
   }

   DocParamList *pl = new DocParamList(this, m_type, d);
   if (m_children.isEmpty()) {
      pl->markFirst();
      pl->markLast();
   } else {
      assert(m_children.last()->kind() == DocNode::Kind_ParamList);
      ((DocParamList *)m_children.last())->markLast(false);
      pl->markLast();
   }
   m_children.append(pl);
   if (xmlContext) {
      retval = pl->parseXml(cmdName);
   } else {
      retval = pl->parse(cmdName);
   }
   if (retval == RetVal_EndParBlock) {
      retval = RetVal_OK;
   }

   DBG(("DocParamSect::parse() end retval=%d\n", retval));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

int DocPara::handleSimpleSection(DocSimpleSect::Type t, bool xmlContext)
{
   DocSimpleSect *ss = 0;
   bool needsSeparator = false;

   if (!m_children.isEmpty() &&                           // previous element
         m_children.last()->kind() == Kind_SimpleSect &&    // was a simple sect
         ((DocSimpleSect *)m_children.last())->type() == t && // of same type
         t != DocSimpleSect::User) {                        // but not user defined

      // append to previous section
      ss = (DocSimpleSect *)m_children.last();
      needsSeparator = true;

   } else { // start new section
      ss = new DocSimpleSect(this, t);
      m_children.append(ss);
   }
   int rv = RetVal_OK;
   if (xmlContext) {
      return ss->parseXml();
   } else {
      rv = ss->parse(t == DocSimpleSect::User, needsSeparator);
   }
   return (rv != TK_NEWPARA) ? rv : RetVal_OK;
}

int DocPara::handleParamSection(const QByteArray &cmdName, DocParamSect::Type t, bool xmlContext = false,
                                int direction = DocParamSect::Unspecified)
{
   DocParamSect *ps = 0;

   if (!m_children.isEmpty() &&                        // previous element
         m_children.last()->kind() == Kind_ParamSect &&  // was a param sect
         ((DocParamSect *)m_children.last())->type() == t) { 

      // of same type
      // append to previous section

      ps = (DocParamSect *)m_children.last();

   } else { // start new section
      ps = new DocParamSect(this, t);
      m_children.append(ps);
   }

   int rv = ps->parse(cmdName, xmlContext, (DocParamSect::Direction)direction);

   return (rv != TK_NEWPARA) ? rv : RetVal_OK;
}

void DocPara::handleCite()
{
   // get the argument of the cite command.
   int tok = doctokenizerYYlex();

   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command", qPrint("cite"));
      return;
   }

   doctokenizerYYsetStateCite();
   tok = doctokenizerYYlex();

   if (tok == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment block while parsing the "
                     "argument of command %s\n", qPrint("cite"));
      return;

   } else if (tok != TK_WORD && tok != TK_LNKWORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint("cite"));
      return;
   }
   g_token->sectionId = g_token->name;
   DocCite *cite = new DocCite(this, g_token->name, s_context);
   m_children.append(cite);
   //cite->parse();

   doctokenizerYYsetStatePara();
}

int DocPara::handleXRefItem()
{
   int retval = doctokenizerYYlex();
   assert(retval == TK_WHITESPACE);
   doctokenizerYYsetStateXRefItem();
   retval = doctokenizerYYlex();
   if (retval == RetVal_OK) {
      DocXRefItem *ref = new DocXRefItem(this, g_token->id, g_token->name);
      if (ref->parse()) {
         m_children.append(ref);
      } else {
         delete ref;
      }
   }
   doctokenizerYYsetStatePara();
   return retval;
}

void DocPara::handleIncludeOperator(const QByteArray &cmdName, DocIncOperator::Type t)
{
   DBG(("handleIncludeOperator(%s)\n", qPrint(cmdName)));
   int tok = doctokenizerYYlex();

   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command", qPrint(cmdName));
      return;
   }

   doctokenizerYYsetStatePattern();
   tok = doctokenizerYYlex();
   doctokenizerYYsetStatePara();

   if (tok == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment block while parsing the "
                     "argument of command %s", qPrint(cmdName));
      return;

   } else if (tok != TK_WORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(cmdName));
      return;
   }

   DocIncOperator *op = new DocIncOperator(this, t, g_token->name, s_context, s_isExample, s_exampleName);
   
   DocNode *n1 = 0;
   DocNode *n2 = 0;

   if (! m_children.isEmpty()) {      
      n1 = m_children[m_children.size() - 1];

      if (m_children.size() > 1) {
         n2 = m_children[m_children.size() - 2];
      }
   }

   // no last node, last node is not operator or whitespace, previous not is not operator
   bool isFirst = n1 == 0 ||  (n1->kind() != DocNode::Kind_IncOperator && n1->kind() != DocNode::Kind_WhiteSpace) || 
                  (n1->kind() == DocNode::Kind_WhiteSpace && n2 != 0 && n2->kind() != DocNode::Kind_IncOperator);

   op->markFirst(isFirst);
   op->markLast(true);

   if (n1 != 0 && n1->kind() == DocNode::Kind_IncOperator) {
      ((DocIncOperator *)n1)->markLast(false);

   } else if (n1 != 0 && n1->kind() == DocNode::Kind_WhiteSpace && n2 != 0 && n2->kind() == DocNode::Kind_IncOperator) {
      ((DocIncOperator *)n2)->markLast(false);
   }

   m_children.append(op);
   op->parse();
}

void DocPara::handleImage(const QByteArray &cmdName)
{
   int tok = doctokenizerYYlex();

   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command", qPrint(cmdName));
      return;
   }

   tok = doctokenizerYYlex();

   if (tok != TK_WORD && tok != TK_LNKWORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(cmdName));
      return;
   }
   tok = doctokenizerYYlex();
   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                     qPrint(cmdName));
      return;
   }
   DocImage::Type t;
   QByteArray imgType = g_token->name.toLower();
   if      (imgType == "html") {
      t = DocImage::Html;
   } else if (imgType == "latex") {
      t = DocImage::Latex;
   } else if (imgType == "docbook") {
      t = DocImage::DocBook;
   } else if (imgType == "rtf") {
      t = DocImage::Rtf;
   } else {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "image type %s specified as the first argument of "
                     "%s is not valid", qPrint(imgType), qPrint(cmdName));
      return;
   }

   doctokenizerYYsetStateFile();
   tok = doctokenizerYYlex();
   doctokenizerYYsetStatePara();

   if (tok != TK_WORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(cmdName));
      return;
   }
   HtmlAttribList attrList;
   DocImage *img = new DocImage(this, attrList, findAndCopyImage(g_token->name, t), t);
   m_children.append(img);
   img->parse();
}

void DocPara::handleDotFile(const QByteArray &cmdName)
{
   int tok = doctokenizerYYlex();
   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                     qPrint(cmdName));
      return;
   }
   doctokenizerYYsetStateFile();
   tok = doctokenizerYYlex();
   doctokenizerYYsetStatePara();
   if (tok != TK_WORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(cmdName));
      return;
   }
   QByteArray name = g_token->name;
   DocDotFile *df = new DocDotFile(this, name, s_context);
   m_children.append(df);
   df->parse();
}

void DocPara::handleMscFile(const QByteArray &cmdName)
{
   int tok = doctokenizerYYlex();
   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                     qPrint(cmdName));
      return;
   }
   doctokenizerYYsetStateFile();
   tok = doctokenizerYYlex();
   doctokenizerYYsetStatePara();
   if (tok != TK_WORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(cmdName));
      return;
   }
   QByteArray name = g_token->name;
   DocMscFile *df = new DocMscFile(this, name, s_context);
   m_children.append(df);
   df->parse();
}

void DocPara::handleDiaFile(const QByteArray &cmdName)
{
   int tok = doctokenizerYYlex();
   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                     qPrint(cmdName));
      return;
   }
   doctokenizerYYsetStateFile();
   tok = doctokenizerYYlex();
   doctokenizerYYsetStatePara();
   if (tok != TK_WORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(cmdName));
      return;
   }
   QByteArray name = g_token->name;
   DocDiaFile *df = new DocDiaFile(this, name, s_context);
   m_children.append(df);
   df->parse();
}

void DocPara::handleLink(const QByteArray &cmdName, bool isJavaLink)
{
   int tok = doctokenizerYYlex();
   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command", qPrint(cmdName));
      return;
   }
   doctokenizerYYsetStateLink();
   tok = doctokenizerYYlex();

   if (tok != TK_WORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "%s as the argument of %s",
                     tokToString(tok), qPrint(cmdName));
      return;
   }
   doctokenizerYYsetStatePara();
   DocLink *lnk = new DocLink(this, g_token->name);
   m_children.append(lnk);

   QByteArray leftOver = lnk->parse(isJavaLink);
   if (!leftOver.isEmpty()) {
      m_children.append(new DocWord(this, leftOver));
   }
}

void DocPara::handleRef(const QByteArray &cmdName)
{
   DBG(("handleRef(%s)\n", qPrint(cmdName)));
   int tok = doctokenizerYYlex();
   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                     qPrint(cmdName));
      return;
   }
   doctokenizerYYsetStateRef();
   tok = doctokenizerYYlex(); // get the reference id
   DocRef *ref = 0;
   if (tok != TK_WORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(cmdName));
      goto endref;
   }
   ref = new DocRef(this, g_token->name, s_context);
   m_children.append(ref);
   ref->parse();
endref:
   doctokenizerYYsetStatePara();
}


void DocPara::handleInclude(const QByteArray &cmdName, DocInclude::Type t)
{
   DBG(("handleInclude(%s)\n", qPrint(cmdName)));
   int tok = doctokenizerYYlex();
   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                     qPrint(cmdName));
      return;
   }
   doctokenizerYYsetStateFile();
   tok = doctokenizerYYlex();
   doctokenizerYYsetStatePara();
   if (tok == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment block while parsing the "
                     "argument of command %s", qPrint(cmdName));
      return;
   } else if (tok != TK_WORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(cmdName));
      return;
   }
   QByteArray fileName = g_token->name;
   QByteArray blockId;
   if (t == DocInclude::Snippet) {
      if (fileName == "this") {
         fileName = s_fileName;
      }
      doctokenizerYYsetStateSnippet();
      tok = doctokenizerYYlex();
      doctokenizerYYsetStatePara();
      if (tok != TK_WORD) {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "expected block identifier, but found token %s instead while parsing the %s command",
                        tokToString(tok), qPrint(cmdName));
         return;
      }
      blockId = "[" + g_token->name + "]";
   }
   DocInclude *inc = new DocInclude(this, fileName, s_context, t, s_isExample, s_exampleName, blockId);
   m_children.append(inc);
   inc->parse();
}

void DocPara::handleSection(const QByteArray &cmdName)
{
   // get the argument of the section command.
   int tok = doctokenizerYYlex();
   if (tok != TK_WHITESPACE) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "expected whitespace after %s command",
                     qPrint(cmdName));
      return;
   }
   tok = doctokenizerYYlex();
   if (tok == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected end of comment block while parsing the "
                     "argument of command %s\n", qPrint(cmdName));
      return;
   } else if (tok != TK_WORD && tok != TK_LNKWORD) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected token %s as the argument of %s",
                     tokToString(tok), qPrint(cmdName));
      return;
   }
   g_token->sectionId = g_token->name;
   doctokenizerYYsetStateSkipTitle();
   doctokenizerYYlex();
   doctokenizerYYsetStatePara();
}

int DocPara::handleHtmlHeader(const HtmlAttribList &tagHtmlAttribs, int level)
{
   DocHtmlHeader *header = new DocHtmlHeader(this, tagHtmlAttribs, level);
   m_children.append(header);
   int retval = header->parse();
   return (retval == RetVal_OK) ? TK_NEWPARA : retval;
}

// For XML tags whose content is stored in attributes rather than
// contained within the element, we need a way to inject the attribute
// text into the current paragraph.
bool DocPara::injectToken(int tok, const QByteArray &tokText)
{
   g_token->name = tokText;
   return defaultHandleToken(this, tok, m_children);
}

int DocPara::handleStartCode()
{
   int retval = doctokenizerYYlex();
   QByteArray lang = g_token->name;
   if (!lang.isEmpty() && lang.at(0) != '.') {
      lang = "." + lang;
   }
   // search for the first non-whitespace line, index is stored in li
   int i = 0, li = 0, l = g_token->verb.length();
   while (i < l && (g_token->verb.at(i) == ' ' || g_token->verb.at(i) == '\n')) {
      if (g_token->verb.at(i) == '\n') {
         li = i + 1;
      }
      i++;
   }
   m_children.append(new DocVerbatim(this, s_context, stripIndentation(g_token->verb.mid(li)), DocVerbatim::Code, s_isExample, s_exampleName, false,
                                     lang));
   if (retval == 0) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "code section ended without end marker");
   }
   doctokenizerYYsetStatePara();
   return retval;
}

void DocPara::handleInheritDoc()
{
   if (s_memberDef) { // inheriting docs from a member
      MemberDef *reMd = s_memberDef->reimplements();

      if (reMd) { // member from which was inherited.
         MemberDef *thisMd = s_memberDef;

         //printf("{InheritDocs:%s=>%s}\n",s_memberDef->qualifiedName().data(),reMd->qualifiedName().data());
         docParserPushContext();
         s_scope = reMd->getOuterScope();

         if (s_scope != Doxygen::globalScope) {
            s_context = s_scope->name();
         }

         s_memberDef = reMd;
         s_styleStack.clear();
         s_nodeStack.clear();
         s_copyStack.append(reMd);
         internalValidatingParseDoc(this, m_children, reMd->briefDescription());
         internalValidatingParseDoc(this, m_children, reMd->documentation());

         s_copyStack.removeOne(reMd);

         docParserPopContext(true);
         s_memberDef = thisMd;
      }
   }
}


int DocPara::handleCommand(const QByteArray &cmdName)
{
   DBG(("handleCommand(%s)\n", qPrint(cmdName)));
   int retval = RetVal_OK;
   int cmdId = Mappers::cmdMapper->map(cmdName);
   switch (cmdId) {
      case CMD_UNKNOWN:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Found unknown command `\\%s'", qPrint(cmdName));
         break;
      case CMD_EMPHASIS:
         m_children.append(new DocStyleChange(this, s_nodeStack.count(), DocStyleChange::Italic, true));
         retval = handleStyleArgument(this, m_children, cmdName);
         m_children.append(new DocStyleChange(this, s_nodeStack.count(), DocStyleChange::Italic, false));
         if (retval != TK_WORD) {
            m_children.append(new DocWhiteSpace(this, " "));
         }
         break;
      case CMD_BOLD:
         m_children.append(new DocStyleChange(this, s_nodeStack.count(), DocStyleChange::Bold, true));
         retval = handleStyleArgument(this, m_children, cmdName);
         m_children.append(new DocStyleChange(this, s_nodeStack.count(), DocStyleChange::Bold, false));
         if (retval != TK_WORD) {
            m_children.append(new DocWhiteSpace(this, " "));
         }
         break;
      case CMD_CODE:
         m_children.append(new DocStyleChange(this, s_nodeStack.count(), DocStyleChange::Code, true));
         retval = handleStyleArgument(this, m_children, cmdName);
         m_children.append(new DocStyleChange(this, s_nodeStack.count(), DocStyleChange::Code, false));
         if (retval != TK_WORD) {
            m_children.append(new DocWhiteSpace(this, " "));
         }
         break;
      case CMD_BSLASH:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_BSlash));
         break;
      case CMD_AT:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_At));
         break;
      case CMD_LESS:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Less));
         break;
      case CMD_GREATER:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Greater));
         break;
      case CMD_AMP:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Amp));
         break;
      case CMD_DOLLAR:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Dollar));
         break;
      case CMD_HASH:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Hash));
         break;
      case CMD_PIPE:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Pipe));
         break;
      case CMD_DCOLON:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_DoubleColon));
         break;
      case CMD_PERCENT:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Percent));
         break;
      case CMD_NDASH:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Minus));
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Minus));
         break;
      case CMD_MDASH:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Minus));
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Minus));
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Minus));
         break;
      case CMD_QUOTE:
         m_children.append(new DocSymbol(this, DocSymbol::Sym_Quot));
         break;
      case CMD_SA:
         s_inSeeBlock = true;
         retval = handleSimpleSection(DocSimpleSect::See);
         s_inSeeBlock = false;
         break;
      case CMD_RETURN:
         retval = handleSimpleSection(DocSimpleSect::Return);
         s_hasReturnCommand = true;
         break;
      case CMD_AUTHOR:
         retval = handleSimpleSection(DocSimpleSect::Author);
         break;
      case CMD_AUTHORS:
         retval = handleSimpleSection(DocSimpleSect::Authors);
         break;
      case CMD_VERSION:
         retval = handleSimpleSection(DocSimpleSect::Version);
         break;
      case CMD_SINCE:
         retval = handleSimpleSection(DocSimpleSect::Since);
         break;
      case CMD_DATE:
         retval = handleSimpleSection(DocSimpleSect::Date);
         break;
      case CMD_NOTE:
         retval = handleSimpleSection(DocSimpleSect::Note);
         break;
      case CMD_WARNING:
         retval = handleSimpleSection(DocSimpleSect::Warning);
         break;
      case CMD_PRE:
         retval = handleSimpleSection(DocSimpleSect::Pre);
         break;
      case CMD_POST:
         retval = handleSimpleSection(DocSimpleSect::Post);
         break;
      case CMD_COPYRIGHT:
         retval = handleSimpleSection(DocSimpleSect::Copyright);
         break;
      case CMD_INVARIANT:
         retval = handleSimpleSection(DocSimpleSect::Invar);
         break;
      case CMD_REMARK:
         retval = handleSimpleSection(DocSimpleSect::Remark);
         break;
      case CMD_ATTENTION:
         retval = handleSimpleSection(DocSimpleSect::Attention);
         break;
      case CMD_PAR:
         retval = handleSimpleSection(DocSimpleSect::User);
         break;
      case CMD_LI: {
         DocSimpleList *sl = new DocSimpleList(this);
         m_children.append(sl);
         retval = sl->parse();
      }
      break;
      case CMD_SECTION: {
         handleSection(cmdName);
         retval = RetVal_Section;
      }
      break;
      case CMD_SUBSECTION: {
         handleSection(cmdName);
         retval = RetVal_Subsection;
      }
      break;
      case CMD_SUBSUBSECTION: {
         handleSection(cmdName);
         retval = RetVal_Subsubsection;
      }
      break;
      case CMD_PARAGRAPH: {
         handleSection(cmdName);
         retval = RetVal_Paragraph;
      }
      break;
      case CMD_STARTCODE: {
         doctokenizerYYsetStateCode();
         retval = handleStartCode();
      }
      break;
      case CMD_HTMLONLY: {
         doctokenizerYYsetStateHtmlOnly();
         retval = doctokenizerYYlex();
         m_children.append(new DocVerbatim(this, s_context, g_token->verb, DocVerbatim::HtmlOnly, s_isExample, s_exampleName, g_token->name == "block"));
         if (retval == 0) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "htmlonly section ended without end marker");
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_MANONLY: {
         doctokenizerYYsetStateManOnly();
         retval = doctokenizerYYlex();
         m_children.append(new DocVerbatim(this, s_context, g_token->verb, DocVerbatim::ManOnly, s_isExample, s_exampleName));
         if (retval == 0) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "manonly section ended without end marker");
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_RTFONLY: {
         doctokenizerYYsetStateRtfOnly();
         retval = doctokenizerYYlex();
         m_children.append(new DocVerbatim(this, s_context, g_token->verb, DocVerbatim::RtfOnly, s_isExample, s_exampleName));
         if (retval == 0) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "rtfonly section ended without end marker");
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_LATEXONLY: {
         doctokenizerYYsetStateLatexOnly();
         retval = doctokenizerYYlex();
         m_children.append(new DocVerbatim(this, s_context, g_token->verb, DocVerbatim::LatexOnly, s_isExample, s_exampleName));
         if (retval == 0) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "latexonly section ended without end marker");
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_XMLONLY: {
         doctokenizerYYsetStateXmlOnly();
         retval = doctokenizerYYlex();
         m_children.append(new DocVerbatim(this, s_context, g_token->verb, DocVerbatim::XmlOnly, s_isExample, s_exampleName));
         if (retval == 0) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "xmlonly section ended without end marker");
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_DBONLY: {
         doctokenizerYYsetStateDbOnly();
         retval = doctokenizerYYlex();
         m_children.append(new DocVerbatim(this, s_context, g_token->verb, DocVerbatim::DocbookOnly, s_isExample, s_exampleName));
         if (retval == 0) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "docbookonly section ended without end marker", doctokenizerYYlineno);
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_VERBATIM: {
         doctokenizerYYsetStateVerbatim();
         retval = doctokenizerYYlex();
         m_children.append(new DocVerbatim(this, s_context, g_token->verb, DocVerbatim::Verbatim, s_isExample, s_exampleName));
         if (retval == 0) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "verbatim section ended without end marker");
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_DOT: {
         doctokenizerYYsetStateDot();
         retval = doctokenizerYYlex();
         m_children.append(new DocVerbatim(this, s_context, g_token->verb, DocVerbatim::Dot, s_isExample, s_exampleName));
         if (retval == 0) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "dot section ended without end marker");
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_MSC: {
         doctokenizerYYsetStateMsc();
         retval = doctokenizerYYlex();
         m_children.append(new DocVerbatim(this, s_context, g_token->verb, DocVerbatim::Msc, s_isExample, s_exampleName));
         if (retval == 0) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "msc section ended without end marker");
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_STARTUML: {
         static QByteArray jarPath = Config_getString("PLANTUML_JAR_PATH");
         doctokenizerYYsetStatePlantUML();
         retval = doctokenizerYYlex();
         if (jarPath.isEmpty()) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "ignoring startuml command because PLANTUML_JAR_PATH is not set");
         } else {
            m_children.append(new DocVerbatim(this, s_context, g_token->verb, DocVerbatim::PlantUML, false, g_token->sectionId));
         }
         if (retval == 0) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "startuml section ended without end marker");
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_ENDPARBLOCK:
         retval = RetVal_EndParBlock;
         break;
      case CMD_ENDCODE:
      case CMD_ENDHTMLONLY:
      case CMD_ENDMANONLY:
      case CMD_ENDRTFONLY:
      case CMD_ENDLATEXONLY:
      case CMD_ENDXMLONLY:
      case CMD_ENDDBONLY:
      case CMD_ENDLINK:
      case CMD_ENDVERBATIM:
      case CMD_ENDDOT:
      case CMD_ENDMSC:
      case CMD_ENDUML:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected command %s", qPrint(g_token->name));
         break;
      case CMD_PARAM:
         retval = handleParamSection(cmdName, DocParamSect::Param, false, g_token->paramDir);
         break;
      case CMD_TPARAM:
         retval = handleParamSection(cmdName, DocParamSect::TemplateParam, false, g_token->paramDir);
         break;
      case CMD_RETVAL:
         retval = handleParamSection(cmdName, DocParamSect::RetVal);
         break;
      case CMD_EXCEPTION:
         retval = handleParamSection(cmdName, DocParamSect::Exception);
         break;
      case CMD_XREFITEM:
         retval = handleXRefItem();
         break;
      case CMD_LINEBREAK: {
         DocLineBreak *lb = new DocLineBreak(this);
         m_children.append(lb);
      }
      break;
      case CMD_ANCHOR: {
         DocAnchor *anchor = handleAnchor(this);
         if (anchor) {
            m_children.append(anchor);
         }
      }
      break;
      case CMD_ADDINDEX: {
         DocIndexEntry *ie = new DocIndexEntry(this,
                                               s_scope != Doxygen::globalScope ? s_scope : 0,
                                               s_memberDef);
         m_children.append(ie);
         retval = ie->parse();
      }
      break;
      case CMD_INTERNAL:
         retval = RetVal_Internal;
         break;
      case CMD_ENDINTERNAL:
         retval = RetVal_EndInternal;
         break;
      case CMD_PARBLOCK: {
         DocParBlock *block = new DocParBlock(this);
         m_children.append(block);
         retval = block->parse();
      }
      break;
      case CMD_COPYDOC:   // fall through
      case CMD_COPYBRIEF: // fall through
      case CMD_COPYDETAILS:
         //retval = RetVal_CopyDoc;
         // these commands should already be resolved by processCopyDoc()
         break;
      case CMD_INCLUDE:
         handleInclude(cmdName, DocInclude::Include);
         break;
      case CMD_INCWITHLINES:
         handleInclude(cmdName, DocInclude::IncWithLines);
         break;
      case CMD_DONTINCLUDE:
         handleInclude(cmdName, DocInclude::DontInclude);
         break;
      case CMD_HTMLINCLUDE:
         handleInclude(cmdName, DocInclude::HtmlInclude);
         break;
      case CMD_LATEXINCLUDE:
         handleInclude(cmdName, DocInclude::LatexInclude);
         break;
      case CMD_VERBINCLUDE:
         handleInclude(cmdName, DocInclude::VerbInclude);
         break;
      case CMD_SNIPPET:
         handleInclude(cmdName, DocInclude::Snippet);
         break;
      case CMD_SKIP:
         handleIncludeOperator(cmdName, DocIncOperator::Skip);
         break;
      case CMD_UNTIL:
         handleIncludeOperator(cmdName, DocIncOperator::Until);
         break;
      case CMD_SKIPLINE:
         handleIncludeOperator(cmdName, DocIncOperator::SkipLine);
         break;
      case CMD_LINE:
         handleIncludeOperator(cmdName, DocIncOperator::Line);
         break;
      case CMD_IMAGE:
         handleImage(cmdName);
         break;
      case CMD_DOTFILE:
         handleDotFile(cmdName);
         break;
    
      case CMD_MSCFILE:
         handleMscFile(cmdName);
         break;
      case CMD_DIAFILE:
         handleDiaFile(cmdName);
         break;
      case CMD_LINK:
         handleLink(cmdName, false);
         break;
      case CMD_JAVALINK:
         handleLink(cmdName, true);
         break;
      case CMD_CITE:
         handleCite();
         break;

      case CMD_REF: // fall through
      case CMD_SUBPAGE:
         handleRef(cmdName);
         break;

      case CMD_SECREFLIST: {
         DocSecRefList *list = new DocSecRefList(this);
         m_children.append(list);
         list->parse();
      }
      break;
      case CMD_SECREFITEM:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected command %s", qPrint(g_token->name));
         break;
      case CMD_ENDSECREFLIST:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "unexpected command %s", qPrint(g_token->name));
         break;
      case CMD_FORMULA: {
         DocFormula *form = new DocFormula(this, g_token->id);
         m_children.append(form);
      }
      break;
      //case CMD_LANGSWITCH:
      //  retval = handleLanguageSwitch();
      //  break;
      case CMD_INTERNALREF:
         //warn_doc_error(s_fileName,doctokenizerYYlineno,"unexpected command %s",qPrint(g_token->name));
      {
         DocInternalRef *ref = handleInternalRef(this);
         if (ref) {
            m_children.append(ref);
            ref->parse();
         }
         doctokenizerYYsetStatePara();
      }
      break;
      case CMD_INHERITDOC:
         handleInheritDoc();
         break;
      default:
         // we should not get here!
         assert(0);
         break;
   }

   INTERNAL_ASSERT(retval == 0 || retval == RetVal_OK || retval == RetVal_SimpleSec ||
                   retval == TK_LISTITEM || retval == TK_ENDLIST || retval == TK_NEWPARA ||
                   retval == RetVal_Section || retval == RetVal_EndList ||
                   retval == RetVal_Internal || retval == RetVal_SwitchLang ||
                   retval == RetVal_EndInternal
                  );
   DBG(("handleCommand(%s) end retval=%x\n", qPrint(cmdName), retval));

   return retval;
}

static bool findAttribute(const HtmlAttribList &tagHtmlAttribs, const char *attrName, QByteArray *result)
{
   for (auto opt : tagHtmlAttribs) { 
      if (opt.name == attrName) {
         *result = opt.value;
         return true;
      }
   }

   return false;
}

int DocPara::handleHtmlStartTag(const QByteArray &tagName, const HtmlAttribList &tagHtmlAttribs)
{
   DBG(("handleHtmlStartTag(%s,%d)\n", qPrint(tagName), tagHtmlAttribs.count()));
   int retval = RetVal_OK;
   int tagId = Mappers::htmlTagMapper->map(tagName);

   if (g_token->emptyTag && !(tagId & XML_CmdMask) &&
         tagId != HTML_UNKNOWN && tagId != HTML_IMG && tagId != HTML_BR) {
      warn_doc_error(s_fileName, doctokenizerYYlineno, "HTML tags may not use the 'empty tag' XHTML syntax.");
   }
   switch (tagId) {
      case HTML_UL: {
         DocHtmlList *list = new DocHtmlList(this, tagHtmlAttribs, DocHtmlList::Unordered);
         m_children.append(list);
         retval = list->parse();
      }
      break;
      case HTML_OL: {
         DocHtmlList *list = new DocHtmlList(this, tagHtmlAttribs, DocHtmlList::Ordered);
         m_children.append(list);
         retval = list->parse();
      }
      break;
      case HTML_LI:
         if (!insideUL(this) && !insideOL(this)) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "lonely <li> tag found");
         } else {
            retval = RetVal_ListItem;
         }
         break;
      case HTML_BOLD:
         handleStyleEnter(this, m_children, DocStyleChange::Bold, &g_token->attribs);
         break;
      case HTML_CODE:
         if (/*getLanguageFromFileName(s_fileName)==SrcLangExt_CSharp ||*/ s_xmlComment)
            // for C# source or inside a <summary> or <remark> section we
            // treat <code> as an XML tag (so similar to @code)
         {
            doctokenizerYYsetStateXmlCode();
            retval = handleStartCode();
         } else { // normal HTML markup
            handleStyleEnter(this, m_children, DocStyleChange::Code, &g_token->attribs);
         }
         break;
      case HTML_EMPHASIS:
         handleStyleEnter(this, m_children, DocStyleChange::Italic, &g_token->attribs);
         break;
      case HTML_DIV:
         handleStyleEnter(this, m_children, DocStyleChange::Div, &g_token->attribs);
         break;
      case HTML_SPAN:
         handleStyleEnter(this, m_children, DocStyleChange::Span, &g_token->attribs);
         break;
      case HTML_SUB:
         handleStyleEnter(this, m_children, DocStyleChange::Subscript, &g_token->attribs);
         break;
      case HTML_SUP:
         handleStyleEnter(this, m_children, DocStyleChange::Superscript, &g_token->attribs);
         break;
      case HTML_CENTER:
         handleStyleEnter(this, m_children, DocStyleChange::Center, &g_token->attribs);
         break;
      case HTML_SMALL:
         handleStyleEnter(this, m_children, DocStyleChange::Small, &g_token->attribs);
         break;
      case HTML_PRE:
         handleStyleEnter(this, m_children, DocStyleChange::Preformatted, &g_token->attribs);
         setInsidePreformatted(true);
         doctokenizerYYsetInsidePre(true);
         break;
      case HTML_P:
         retval = TK_NEWPARA;
         break;
      case HTML_DL: {
         DocHtmlDescList *list = new DocHtmlDescList(this, tagHtmlAttribs);
         m_children.append(list);
         retval = list->parse();
      }
      break;
      case HTML_DT:
         retval = RetVal_DescTitle;
         break;
      case HTML_DD:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag <dd> found");
         break;
      case HTML_TABLE: {
         DocHtmlTable *table = new DocHtmlTable(this, tagHtmlAttribs);
         m_children.append(table);
         retval = table->parse();
      }
      break;
      case HTML_TR:
         retval = RetVal_TableRow;
         break;
      case HTML_TD:
         retval = RetVal_TableCell;
         break;
      case HTML_TH:
         retval = RetVal_TableHCell;
         break;
      case HTML_CAPTION:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag <caption> found");
         break;
      case HTML_BR: {
         DocLineBreak *lb = new DocLineBreak(this);
         m_children.append(lb);
      }
      break;
      case HTML_HR: {
         DocHorRuler *hr = new DocHorRuler(this);
         m_children.append(hr);
      }
      break;
      case HTML_A:
         retval = handleAHref(this, m_children, tagHtmlAttribs);
         break;
      case HTML_H1:
         retval = handleHtmlHeader(tagHtmlAttribs, 1);
         break;
      case HTML_H2:
         retval = handleHtmlHeader(tagHtmlAttribs, 2);
         break;
      case HTML_H3:
         retval = handleHtmlHeader(tagHtmlAttribs, 3);
         break;
      case HTML_H4:
         retval = handleHtmlHeader(tagHtmlAttribs, 4);
         break;
      case HTML_H5:
         retval = handleHtmlHeader(tagHtmlAttribs, 5);
         break;
      case HTML_H6:
         retval = handleHtmlHeader(tagHtmlAttribs, 6);
         break;
      case HTML_IMG: {
         handleImg(this, m_children, tagHtmlAttribs);
      }
      break;
      case HTML_BLOCKQUOTE: {
         DocHtmlBlockQuote *block = new DocHtmlBlockQuote(this, tagHtmlAttribs);
         m_children.append(block);
         retval = block->parse();
      }
      break;

      case XML_SUMMARY:
      case XML_REMARKS:
         s_xmlComment = true;
      // fall through
      case XML_VALUE:
      case XML_PARA:
         if (!m_children.isEmpty()) {
            retval = TK_NEWPARA;
         }
         break;
      case XML_EXAMPLE:
      case XML_DESCRIPTION:
         if (insideTable(this)) {
            retval = RetVal_TableCell;
         }
         break;
      case XML_C:
         handleStyleEnter(this, m_children, DocStyleChange::Code, &g_token->attribs);
         break;
      case XML_PARAM:
      case XML_TYPEPARAM: {
         QByteArray paramName;
         if (findAttribute(tagHtmlAttribs, "name", &paramName)) {
            if (paramName.isEmpty()) {
               if (Config_getBool("WARN_NO_PARAMDOC")) {
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "empty 'name' attribute for <param> tag.");
               }
            } else {
               retval = handleParamSection(paramName,
                                           tagId == XML_PARAM ? DocParamSect::Param : DocParamSect::TemplateParam,
                                           true);
            }
         } else {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "Missing 'name' attribute from <param> tag.");
         }
      }
      break;
      case XML_PARAMREF:
      case XML_TYPEPARAMREF: {
         QByteArray paramName;
         if (findAttribute(tagHtmlAttribs, "name", &paramName)) {
            //printf("paramName=%s\n",paramName.data());
            m_children.append(new DocStyleChange(this, s_nodeStack.count(), DocStyleChange::Italic, true));
            m_children.append(new DocWord(this, paramName));
            m_children.append(new DocStyleChange(this, s_nodeStack.count(), DocStyleChange::Italic, false));
            if (retval != TK_WORD) {
               m_children.append(new DocWhiteSpace(this, " "));
            }
         } else {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "Missing 'name' attribute from <param%sref> tag.", tagId == XML_PARAMREF ? "" : "type");
         }
      }
      break;
      case XML_EXCEPTION: {
         QByteArray exceptName;
         if (findAttribute(tagHtmlAttribs, "cref", &exceptName)) {
            retval = handleParamSection(exceptName, DocParamSect::Exception, true);
         } else {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "Missing 'name' attribute from <exception> tag.");
         }
      }

      break;
      case XML_ITEM:
      case XML_LISTHEADER:
         if (insideTable(this)) {
            retval = RetVal_TableRow;
         } else if (insideUL(this) || insideOL(this)) {
            retval = RetVal_ListItem;
         } else {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "lonely <item> tag found");
         }
         break;

      case XML_RETURNS:
         retval = handleSimpleSection(DocSimpleSect::Return, true);
         s_hasReturnCommand = true;
         break;

      case XML_TERM:
         //m_children.append(new DocStyleChange(this,s_nodeStack.count(),DocStyleChange::Bold,true));
         if (insideTable(this)) {
            retval = RetVal_TableCell;
         }
         break;

      case XML_SEE:
         // not sure if <see> is the same as <seealso> or if it should you link a member
         // without producing a section. The C# specification is extremely vague about this 
      {
         QByteArray cref;
         
         if (findAttribute(tagHtmlAttribs, "cref", &cref)) {
            if (g_token->emptyTag) { 
               // <see cref="..."/> style

               bool inSeeBlock = s_inSeeBlock;
               g_token->name = cref;
               s_inSeeBlock = true;
               handleLinkedWord(this, m_children);
               s_inSeeBlock = inSeeBlock;

            } else { 
               // <see cref="...">...</see> style
              
               doctokenizerYYsetStatePara();
               DocLink *lnk = new DocLink(this, cref);
               m_children.append(lnk);

               QByteArray leftOver = lnk->parse(false, true);
               if (!leftOver.isEmpty()) {
                  m_children.append(new DocWord(this, leftOver));
               }
            }

         } else {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "Missing 'cref' attribute from <see> tag.");
         }
      }
      break;

      case XML_SEEALSO: {
         QByteArray cref;
         if (findAttribute(tagHtmlAttribs, "cref", &cref)) {
            // Look for an existing "see" section
            DocSimpleSect *ss = 0;
           
            for (auto n : m_children) {
               if (n->kind() == Kind_SimpleSect && ((DocSimpleSect *)n)->type() == DocSimpleSect::See) {
                  ss = (DocSimpleSect *)n;
               }
            }

            if (!ss) { // start new section
               ss = new DocSimpleSect(this, DocSimpleSect::See);
               m_children.append(ss);
            }

            ss->appendLinkWord(cref);
            retval = RetVal_OK;
         } else {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "Missing 'cref' attribute from <seealso> tag.");
         }
      }
      break;
      case XML_LIST: {
         QByteArray type;
         findAttribute(tagHtmlAttribs, "type", &type);
         DocHtmlList::Type listType = DocHtmlList::Unordered;
         HtmlAttribList emptyList;
         if (type == "number") {
            listType = DocHtmlList::Ordered;
         }
         if (type == "table") {
            DocHtmlTable *table = new DocHtmlTable(this, emptyList);
            m_children.append(table);
            retval = table->parseXml();
         } else {
            DocHtmlList *list = new DocHtmlList(this, emptyList, listType);
            m_children.append(list);
            retval = list->parseXml();
         }
      }
      break;
      case XML_INCLUDE:
      case XML_PERMISSION:
         // These tags are defined in .Net but are currently unsupported
         break;
      case HTML_UNKNOWN:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported xml/html tag <%s> found", qPrint(tagName));
         m_children.append(new DocWord(this, "<" + tagName + tagHtmlAttribs.toString() + ">"));
         break;
      case XML_INHERITDOC:
         handleInheritDoc();
         break;

      default:
         // we should not get here!
         assert(0);
         break;
   }
   return retval;
}

int DocPara::handleHtmlEndTag(const QByteArray &tagName)
{
   DBG(("handleHtmlEndTag(%s)\n", qPrint(tagName)));
   int tagId = Mappers::htmlTagMapper->map(tagName);
   int retval = RetVal_OK;
   switch (tagId) {
      case HTML_UL:
         if (!insideUL(this)) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "found </ul> tag without matching <ul>");
         } else {
            retval = RetVal_EndList;
         }
         break;
      case HTML_OL:
         if (!insideOL(this)) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "found </ol> tag without matching <ol>");
         } else {
            retval = RetVal_EndList;
         }
         break;
      case HTML_LI:
         if (!insideLI(this)) {
            warn_doc_error(s_fileName, doctokenizerYYlineno, "found </li> tag without matching <li>");
         } else {
            // ignore </li> tags
         }
         break;
      case HTML_BLOCKQUOTE:
         retval = RetVal_EndBlockQuote;
         break;
      //case HTML_PRE:
      //  if (!insidePRE(this))
      //  {
      //    warn_doc_error(s_fileName,doctokenizerYYlineno,"found </pre> tag without matching <pre>");
      //  }
      //  else
      //  {
      //    retval=RetVal_EndPre;
      //  }
      //  break;
      case HTML_BOLD:
         handleStyleLeave(this, m_children, DocStyleChange::Bold, "b");
         break;
      case HTML_CODE:
         handleStyleLeave(this, m_children, DocStyleChange::Code, "code");
         break;
      case HTML_EMPHASIS:
         handleStyleLeave(this, m_children, DocStyleChange::Italic, "em");
         break;
      case HTML_DIV:
         handleStyleLeave(this, m_children, DocStyleChange::Div, "div");
         break;
      case HTML_SPAN:
         handleStyleLeave(this, m_children, DocStyleChange::Span, "span");
         break;
      case HTML_SUB:
         handleStyleLeave(this, m_children, DocStyleChange::Subscript, "sub");
         break;
      case HTML_SUP:
         handleStyleLeave(this, m_children, DocStyleChange::Superscript, "sup");
         break;
      case HTML_CENTER:
         handleStyleLeave(this, m_children, DocStyleChange::Center, "center");
         break;
      case HTML_SMALL:
         handleStyleLeave(this, m_children, DocStyleChange::Small, "small");
         break;
      case HTML_PRE:
         handleStyleLeave(this, m_children, DocStyleChange::Preformatted, "pre");
         setInsidePreformatted(false);
         doctokenizerYYsetInsidePre(false);
         break;
      case HTML_P:
         retval = TK_NEWPARA;
         break;
      case HTML_DL:
         retval = RetVal_EndDesc;
         break;
      case HTML_DT:
         // ignore </dt> tag
         break;
      case HTML_DD:
         // ignore </dd> tag
         break;
      case HTML_TABLE:
         retval = RetVal_EndTable;
         break;
      case HTML_TR:
         // ignore </tr> tag
         break;
      case HTML_TD:
         // ignore </td> tag
         break;
      case HTML_TH:
         // ignore </th> tag
         break;
      case HTML_CAPTION:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag </caption> found");
         break;
      case HTML_BR:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Illegal </br> tag found\n");
         break;
      case HTML_H1:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag </h1> found");
         break;
      case HTML_H2:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag </h2> found");
         break;
      case HTML_H3:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag </h3> found");
         break;
      case HTML_H4:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag </h4> found");
         break;
      case HTML_H5:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag </h5> found");
         break;
      case HTML_H6:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag </h6> found");
         break;
      case HTML_IMG:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag </img> found");
         break;
      case HTML_HR:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected tag </hr> found");
         break;
      case HTML_A:
         //warn_doc_error(s_fileName,doctokenizerYYlineno,"Unexpected tag </a> found");
         // ignore </a> tag (can be part of <a name=...></a>
         break;

      case XML_TERM:
         //m_children.append(new DocStyleChange(this,s_nodeStack.count(),DocStyleChange::Bold,false));
         break;
      case XML_SUMMARY:
      case XML_REMARKS:
      case XML_PARA:
      case XML_VALUE:
      case XML_LIST:
      case XML_EXAMPLE:
      case XML_PARAM:
      case XML_TYPEPARAM:
      case XML_RETURNS:
      case XML_SEE:
      case XML_SEEALSO:
      case XML_EXCEPTION:
      case XML_INHERITDOC:
         retval = RetVal_CloseXml;
         break;
      case XML_C:
         handleStyleLeave(this, m_children, DocStyleChange::Code, "c");
         break;
      case XML_ITEM:
      case XML_LISTHEADER:
      case XML_INCLUDE:
      case XML_PERMISSION:
      case XML_DESCRIPTION:
      case XML_PARAMREF:
      case XML_TYPEPARAMREF:
         // These tags are defined in .Net but are currently unsupported
         break;
      case HTML_UNKNOWN:
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported xml/html tag </%s> found", qPrint(tagName));
         m_children.append(new DocWord(this, "</" + tagName + ">"));
         break;
      default:
         // we should not get here!
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected end tag %s\n", qPrint(tagName));
         assert(0);
         break;
   }
   return retval;
}

int DocPara::parse()
{
   DBG(("DocPara::parse() start\n"));
   s_nodeStack.push(this);
   // handle style commands "inherited" from the previous paragraph
   handleInitialStyleCommands(this, m_children);
   int tok;
   int retval = 0;
   while ((tok = doctokenizerYYlex())) { // get the next token
   reparsetoken:
      DBG(("token %s at %d", tokToString(tok), doctokenizerYYlineno));
      if (tok == TK_WORD || tok == TK_LNKWORD || tok == TK_SYMBOL || tok == TK_URL ||
            tok == TK_COMMAND || tok == TK_HTMLTAG
         ) {
         DBG((" name=%s", qPrint(g_token->name)));
      }
      DBG(("\n"));
      switch (tok) {
         case TK_WORD:
            m_children.append(new DocWord(this, g_token->name));
            break;
         case TK_LNKWORD:
            handleLinkedWord(this, m_children);
            break;
         case TK_URL:
            m_children.append(new DocURL(this, g_token->name, g_token->isEMailAddr));
            break;
         case TK_WHITESPACE: {
            // prevent leading whitespace and collapse multiple whitespace areas
            DocNode::Kind k;

            if (insidePRE(this) || // all whitespace is relevant
                  (
                     // remove leading whitespace
                     !m_children.isEmpty()  &&
                     // and whitespace after certain constructs
                     (k = m_children.last()->kind()) != DocNode::Kind_HtmlDescList &&
                     k != DocNode::Kind_HtmlTable &&
                     k != DocNode::Kind_HtmlList &&
                     k != DocNode::Kind_SimpleSect &&
                     k != DocNode::Kind_AutoList &&
                     k != DocNode::Kind_SimpleList &&
                     /*k!=DocNode::Kind_Verbatim &&*/
                     k != DocNode::Kind_HtmlHeader &&
                     k != DocNode::Kind_HtmlBlockQuote &&
                     k != DocNode::Kind_ParamSect &&
                     k != DocNode::Kind_XRefItem)) {
               m_children.append(new DocWhiteSpace(this, g_token->chars));
            }
         }
         break;

         case TK_LISTITEM: {
            DBG(("found list item at %d parent=%d\n", g_token->indent, parent()->kind()));
            DocNode *n = parent();
            while (n && n->kind() != DocNode::Kind_AutoList) {
               n = n->parent();
            }
            if (n) { // we found an auto list up in the hierarchy
               DocAutoList *al = (DocAutoList *)n;
               DBG(("previous list item at %d\n", al->indent()));
               if (al->indent() >= g_token->indent)
                  // new item at the same or lower indent level
               {
                  retval = TK_LISTITEM;
                  goto endparagraph;
               }
            }

            // determine list depth
            int depth = 0;
            n = parent();
            while (n) {
               if (n->kind() == DocNode::Kind_AutoList &&
                     ((DocAutoList *)n)->isEnumList()) {
                  depth++;
               }
               n = n->parent();
            }

            // first item or sub list => create new list
            DocAutoList *al = 0;
            do {
               al = new DocAutoList(this, g_token->indent,
                                    g_token->isEnumList, depth);
               m_children.append(al);
               retval = al->parse();
            } while (retval == TK_LISTITEM &&       // new list
                     al->indent() == g_token->indent // at same indent level
                    );

            // check the return value
            if (retval == RetVal_SimpleSec) { // auto list ended due to simple section command
               // Reparse the token that ended the section at this level,
               // so a new simple section will be started at this level.
               // This is the same as unputting the last read token and continuing.
               g_token->name = g_token->simpleSectName;
               if (g_token->name.left(4) == "rcs:") { // RCS section
                  g_token->name = g_token->name.mid(4);
                  g_token->text = g_token->simpleSectText;
                  tok = TK_RCSTAG;
               } else { // other section
                  tok = TK_COMMAND;
               }
               DBG(("reparsing command %s\n", qPrint(g_token->name)));
               goto reparsetoken;
            } else if (retval == TK_ENDLIST) {
               if (al->indent() > g_token->indent) { // end list
                  goto endparagraph;
               } else { // continue with current paragraph
               }
            } else { // paragraph ended due to TK_NEWPARA, TK_LISTITEM, or EOF
               goto endparagraph;
            }
         }
         break;
         case TK_ENDLIST:
            DBG(("Found end of list inside of paragraph at line %d\n", doctokenizerYYlineno));
            if (parent()->kind() == DocNode::Kind_AutoListItem) {
               assert(parent()->parent()->kind() == DocNode::Kind_AutoList);
               DocAutoList *al = (DocAutoList *)parent()->parent();
               if (al->indent() >= g_token->indent) {
                  // end of list marker ends this paragraph
                  retval = TK_ENDLIST;
                  goto endparagraph;
               } else {
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "End of list marker found "
                                 "has invalid indent level");
               }
            } else {
               warn_doc_error(s_fileName, doctokenizerYYlineno, "End of list marker found without any preceding "
                              "list items");
            }
            break;

         case TK_COMMAND: {
            // see if we have to start a simple section
            int cmd = Mappers::cmdMapper->map(g_token->name);
            DocNode *n = parent();

            while (n && n->kind() != DocNode::Kind_SimpleSect && n->kind() != DocNode::Kind_ParamSect) {
               n = n->parent();
            }

            if (cmd & SIMPLESECT_BIT) {
               if (n) { // already in a simple section
                  // simple section cannot start in this paragraph, need
                  // to unwind the stack and remember the command.
                  g_token->simpleSectName = g_token->name;
                  retval = RetVal_SimpleSec;
                  goto endparagraph;
               }
            }

            // see if we are in a simple list
            n = parent();
            while (n && n->kind() != DocNode::Kind_SimpleListItem) {
               n = n->parent();
            }
            if (n) {
               if (cmd == CMD_LI) {
                  retval = RetVal_ListItem;
                  goto endparagraph;
               }
            }

            // handle the command
            retval = handleCommand(g_token->name);
            DBG(("handleCommand returns %x\n", retval));

            // check the return value
            if (retval == RetVal_SimpleSec) {
               // Reparse the token that ended the section at this level,
               // so a new simple section will be started at this level.
               // This is the same as unputting the last read token and continuing.
               g_token->name = g_token->simpleSectName;
               if (g_token->name.left(4) == "rcs:") { // RCS section
                  g_token->name = g_token->name.mid(4);
                  g_token->text = g_token->simpleSectText;
                  tok = TK_RCSTAG;
               } else { // other section
                  tok = TK_COMMAND;
               }
               DBG(("reparsing command %s\n", qPrint(g_token->name)));
               goto reparsetoken;
            } else if (retval == RetVal_OK) {
               // the command ended normally, keep scanning for new tokens.
               retval = 0;
            } else if (retval > 0 && retval < RetVal_OK) {
               // the command ended with a new command, reparse this token
               tok = retval;
               goto reparsetoken;
            } else // end of file, end of paragraph, start or end of section
               // or some auto list marker
            {
               goto endparagraph;
            }
         }
         break;
         case TK_HTMLTAG: {
            if (!g_token->endTag) { // found a start tag
               retval = handleHtmlStartTag(g_token->name, g_token->attribs);
            } else { // found an end tag
               retval = handleHtmlEndTag(g_token->name);
            }
            if (retval == RetVal_OK) {
               // the command ended normally, keep scanner for new tokens.
               retval = 0;
            } else {
               goto endparagraph;
            }
         }
         break;
         case TK_SYMBOL: {
            DocSymbol::SymType s = DocSymbol::decodeSymbol(g_token->name);
            if (s != DocSymbol::Sym_Unknown) {
               m_children.append(new DocSymbol(this, s));
            } else {
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found",
                              qPrint(g_token->name));
            }
            break;
         }
         case TK_NEWPARA:
            retval = TK_NEWPARA;
            goto endparagraph;
         case TK_RCSTAG: {
            DocNode *n = parent();
            while (n &&
                   n->kind() != DocNode::Kind_SimpleSect &&
                   n->kind() != DocNode::Kind_ParamSect
                  ) {
               n = n->parent();
            }
            if (n) { // already in a simple section
               // simple section cannot start in this paragraph, need
               // to unwind the stack and remember the command.
               g_token->simpleSectName = "rcs:" + g_token->name;
               g_token->simpleSectText = g_token->text;
               retval = RetVal_SimpleSec;
               goto endparagraph;
            }

            // see if we are in a simple list
            DocSimpleSect *ss = new DocSimpleSect(this, DocSimpleSect::Rcs);
            m_children.append(ss);
            ss->parseRcs();
         }
         break;
         default:
            warn_doc_error(s_fileName, doctokenizerYYlineno,
                           "Found unexpected token (id=%x)\n", tok);
            break;
      }
   }
   retval = 0;

endparagraph:
   handlePendingStyleCommands(this, m_children);
   DocNode *n = s_nodeStack.pop();
   assert(n == this);

   DBG(("DocPara::parse() end retval=%x\n", retval));
   INTERNAL_ASSERT(retval == 0 || retval == TK_NEWPARA || retval == TK_LISTITEM ||
                   retval == TK_ENDLIST || retval > RetVal_OK );

   return retval;
}

int DocSection::parse()
{
   DBG(("DocSection::parse() start %s level=%d\n", qPrint(g_token->sectionId), m_level));
   int retval = RetVal_OK;
   s_nodeStack.push(this);

   QSharedPointer<SectionInfo>sec;

   if (!m_id.isEmpty()) {
      sec = Doxygen::sectionDict->find(m_id);

      if (sec) {
         m_file   = sec->fileName;
         m_anchor = sec->label;
         m_title  = sec->title;

         if (m_title.isEmpty()) {
            m_title = sec->label;
         }

         if (s_sectionDict && s_sectionDict->find(m_id) == 0) {
            s_sectionDict->insert(m_id, sec);
         }
      }
   }

   // first parse any number of paragraphs
   bool isFirst = true;
   DocPara *lastPar = 0;

   do {
      DocPara *par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }

      retval = par->parse();
      if (!par->isEmpty()) {
         m_children.append(par);
         lastPar = par;
      } else {
         delete par;
      }

      if (retval == TK_LISTITEM) {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Invalid list item found");
      }

      if (retval == RetVal_Internal) {
         DocInternal *in = new DocInternal(this);
         m_children.append(in);
         retval = in->parse(m_level + 1);
         if (retval == RetVal_EndInternal) {
            retval = RetVal_OK;
         }
      }
   } while (retval != 0 &&
            retval != RetVal_Section       &&
            retval != RetVal_Subsection    &&
            retval != RetVal_Subsubsection &&
            retval != RetVal_Paragraph     &&
            retval != RetVal_EndInternal
           );

   if (lastPar) {
      lastPar->markLast();
   }

   //printf("m_level=%d <-> %d\n",m_level,Doxygen::subpageNestingLevel);

   if (retval == RetVal_Subsection && m_level == Doxygen::subpageNestingLevel + 1) {
      // then parse any number of nested sections
      while (retval == RetVal_Subsection) { // more sections follow
         //SectionInfo *sec=Doxygen::sectionDict[g_token->sectionId];
         DocSection *s = new DocSection(this,
                                        qMin(2 + Doxygen::subpageNestingLevel, 5), g_token->sectionId);
         m_children.append(s);
         retval = s->parse();
      }
   } else if (retval == RetVal_Subsubsection && m_level == Doxygen::subpageNestingLevel + 2) {
      // then parse any number of nested sections
      while (retval == RetVal_Subsubsection) { // more sections follow
         //SectionInfo *sec=Doxygen::sectionDict[g_token->sectionId];
         DocSection *s = new DocSection(this,
                                        qMin(3 + Doxygen::subpageNestingLevel, 5), g_token->sectionId);
         m_children.append(s);
         retval = s->parse();
      }
   } else if (retval == RetVal_Paragraph && m_level == qMin(5, Doxygen::subpageNestingLevel + 3)) {
      // then parse any number of nested sections
      while (retval == RetVal_Paragraph) { // more sections follow
         //SectionInfo *sec=Doxygen::sectionDict[g_token->sectionId];
         DocSection *s = new DocSection(this,
                                        qMin(4 + Doxygen::subpageNestingLevel, 5), g_token->sectionId);
         m_children.append(s);
         retval = s->parse();
      }
   } else if ((m_level <= 1 + Doxygen::subpageNestingLevel && retval == RetVal_Subsubsection) ||
              (m_level <= 2 + Doxygen::subpageNestingLevel && retval == RetVal_Paragraph)
             ) {
      int level = (retval == RetVal_Subsubsection) ? 3 : 4;
      warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected %s "
                     "command found inside %s!",
                     sectionLevelToName[level], sectionLevelToName[m_level]);
      retval = 0; // stop parsing
   } else {
   }

   INTERNAL_ASSERT(retval == 0 ||
                   retval == RetVal_Section ||
                   retval == RetVal_Subsection ||
                   retval == RetVal_Subsubsection ||
                   retval == RetVal_Paragraph ||
                   retval == RetVal_Internal ||
                   retval == RetVal_EndInternal
                  );

   DBG(("DocSection::parse() end: retval=%x\n", retval));
   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   return retval;
}

//--------------------------------------------------------------------------

void DocText::parse()
{
   DBG(("DocText::parse() start\n"));

   s_nodeStack.push(this);
   doctokenizerYYsetStateText();

   int tok;
 
   while ((tok = doctokenizerYYlex())) { 
      // get the next token

      switch (tok) {
         case TK_WORD:

            m_children.append(new DocWord(this, g_token->name));
            break;

         case TK_WHITESPACE:
            m_children.append(new DocWhiteSpace(this, g_token->chars));
            break;

         case TK_SYMBOL: {
            DocSymbol::SymType s = DocSymbol::decodeSymbol(g_token->name);

            if (s != DocSymbol::Sym_Unknown) {
               m_children.append(new DocSymbol(this, s));
            } else {
               warn_doc_error(s_fileName, doctokenizerYYlineno, "Unsupported symbol %s found", qPrint(g_token->name));
            }
         }
         break;

         case TK_COMMAND:

            switch (Mappers::cmdMapper->map(g_token->name)) {
               case CMD_BSLASH:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_BSlash));
                  break;
               case CMD_AT:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_At));
                  break;
               case CMD_LESS:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Less));
                  break;
               case CMD_GREATER:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Greater));
                  break;
               case CMD_AMP:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Amp));
                  break;
               case CMD_DOLLAR:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Dollar));
                  break;
               case CMD_HASH:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Hash));
                  break;
               case CMD_DCOLON:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_DoubleColon));
                  break;
               case CMD_PERCENT:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Percent));
                  break;
               case CMD_NDASH:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Minus));
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Minus));
                  break;
               case CMD_MDASH:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Minus));
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Minus));
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Minus));
                  break;
               case CMD_QUOTE:
                  m_children.append(new DocSymbol(this, DocSymbol::Sym_Quot));
                  break;
               default:
                  warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected command `%s' found", qPrint(g_token->name));
                  break;
            }
            break;

         default:
            warn_doc_error(s_fileName, doctokenizerYYlineno, "Unexpected token %s", tokToString(tok));
            break;
      }
   }

   handleUnclosedStyleCommands();

   DocNode *n = s_nodeStack.pop();
   assert(n == this);

   DBG(("DocText::parse() end\n"));
}


//--------------------------------------------------------------------------

void DocRoot::parse()
{
   DBG(("DocRoot::parse() start\n"));
   s_nodeStack.push(this);
   doctokenizerYYsetStatePara();
   int retval = 0;

   // first parse any number of paragraphs
   bool isFirst = true;
   DocPara *lastPar = 0;
   do {
      DocPara *par = new DocPara(this);
      if (isFirst) {
         par->markFirst();
         isFirst = false;
      }
      retval = par->parse();
      if (!par->isEmpty()) {
         m_children.append(par);
         lastPar = par;
      } else {
         delete par;
      }
      if (retval == TK_LISTITEM) {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Invalid list item found");
      } else if (retval == RetVal_Subsection) {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "found subsection command outside of section context!");
      } else if (retval == RetVal_Subsubsection) {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "found subsubsection command outside of subsection context!");
      } else if (retval == RetVal_Paragraph) {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "found paragraph command outside of subsubsection context!");
      }
      if (retval == RetVal_Internal) {
         DocInternal *in = new DocInternal(this);
         m_children.append(in);
         retval = in->parse(1);
      }
   } while (retval != 0 && retval != RetVal_Section);
   if (lastPar) {
      lastPar->markLast();
   }

   //printf("DocRoot::parse() retval=%d %d\n",retval,RetVal_Section);
   // then parse any number of level1 sections
   while (retval == RetVal_Section) {
      QSharedPointer<SectionInfo> sec = Doxygen::sectionDict->find(g_token->sectionId);

      if (sec) {
         DocSection *s = new DocSection(this, qMin(1 + Doxygen::subpageNestingLevel, 5), g_token->sectionId);
         m_children.append(s);
         retval = s->parse();

      } else {
         warn_doc_error(s_fileName, doctokenizerYYlineno, "Invalid section id `%s'; ignoring section", qPrint(g_token->sectionId));
         retval = 0;
      }
   }

   handleUnclosedStyleCommands();

   DocNode *n = s_nodeStack.pop();
   assert(n == this);
   DBG(("DocRoot::parse() end\n"));
}

static QByteArray extractCopyDocId(const char *data, uint &j, uint len)
{
   uint s = j;
   uint e = j;

   int round = 0;

   bool insideDQuote = false;
   bool insideSQuote = false;
   bool found = false;

   while (j < len && !found) {
      if (!insideSQuote && !insideDQuote) {
         switch (data[j]) {
            case '(':
               round++;
               break;
            case ')':
               round--;
               break;
            case '"':
               insideDQuote = true;
               break;
            case '\'':
               insideSQuote = true;
               break;
            case ' ':  // fall through
            case '\t': // fall through
            case '\n':
               found = (round == 0);
               break;
         }
      } else if (insideSQuote) { // look for single quote end
         if (data[j] == '\'' && (j == 0 || data[j] != '\\')) {
            insideSQuote = false;
         }
      } else if (insideDQuote) { // look for double quote end
         if (data[j] == '"' && (j == 0 || data[j] != '\\')) {
            insideDQuote = false;
         }
      }

      if (!found) {
         j++;
      }
   }

   if (qstrncmp(data + j, " const", 6) == 0) {
      j += 6;

   } else if (qstrncmp(data + j, " volatile", 9) == 0) {
      j += 9;
   }

   e = j;
   QByteArray id;
   id.resize(e - s + 1);

   if (e > s) {
      memcpy(id.data(), data + s, e - s);
   }

   id[e - s] = '\0';
   
   return id;
}

static uint isCopyBriefOrDetailsCmd(const char *data, uint i, uint len, bool &brief)
{
   int j = 0;

   if (i == 0 || (data[i - 1] != '@' && data[i - 1] != '\\')) { // not an escaped command
      if (i + 10 < len && qstrncmp(data + i + 1, "copybrief", 9) == 0) { // @copybrief or \copybrief
         j = i + 10;
         brief = true;
      } else if (i + 12 < len && qstrncmp(data + i + 1, "copydetails", 11) == 0) { // @copydetails or \copydetails
         j = i + 12;
         brief = false;
      }
   }
   return j;
}

static QByteArray processCopyDoc(const char *data, uint &len)
{   
   GrowBuf buf;
   uint i = 0;

   while (i < len) {
      char c = data[i];

      if (c == '@' || c == '\\') { // look for a command
         bool isBrief = true;
         uint j = isCopyBriefOrDetailsCmd(data, i, len, isBrief);

         if (j > 0) {
            // skip whitespace
            while (j < len && (data[j] == ' ' || data[j] == '\t')) {
               j++;
            }

            // extract the argument
            QByteArray id = extractCopyDocId(data, j, len);
            Definition *def;

            QByteArray doc, brief;
 
            if (findDocsForMemberOrCompound(id, &doc, &brief, &def)) {               
               if (s_copyStack.indexOf(def) == -1) { // definition not parsed earlier
                  s_copyStack.append(def);

                  if (isBrief) {
                     uint l = brief.length();
                     buf.addStr(processCopyDoc(brief, l));

                  } else {
                     uint l = doc.length();
                     buf.addStr(processCopyDoc(doc, l));
                  }

                  s_copyStack.removeOne(def);

               } else {
                  warn_doc_error(s_fileName, doctokenizerYYlineno,
                                 "Found recursive @copy%s or @copydoc relation for argument '%s'.\n",
                                 isBrief ? "brief" : "details", id.data());
               }
            } else {
               warn_doc_error(s_fileName, doctokenizerYYlineno,
                              "@copy%s or @copydoc target '%s' not found", isBrief ? "brief" : "details",
                              id.data());
            }

            // skip over command
            i = j;

         } else {
            buf.addChar(c);
            i++;
         }

      } else { // not a command, just copy
         buf.addChar(c);
         i++;
      }
   }
   len = buf.getPos();
   buf.addChar(0);
   return buf.get();
}

DocRoot *validatingParseDoc(const char *fileName, int startLine, Definition *ctx, MemberDef *md, const char *input, bool indexWords,
                            bool isExample, const char *exampleName, bool singleLine, bool linkFromIndex)
{  
   // store parser state so we can re-enter this function if needed
   // bool fortranOpt = Config_getBool("OPTIMIZE_FOR_FORTRAN");
   docParserPushContext();

   if (ctx && ctx != Doxygen::globalScope &&
         (ctx->definitionType() == Definition::TypeClass ||
          ctx->definitionType() == Definition::TypeNamespace)) {
      s_context = ctx->name();

   } else if (ctx && ctx->definitionType() == Definition::TypePage) {
      Definition *scope = ((PageDef *)ctx)->getPageScope();

      if (scope && scope != Doxygen::globalScope) {
         s_context = scope->name();
      }

   } else if (ctx && ctx->definitionType() == Definition::TypeGroup) {
      Definition *scope = ((GroupDef *)ctx)->getGroupScope();

      if (scope && scope != Doxygen::globalScope) {
         s_context = scope->name();
      }

   } else {
      s_context = "";
   }

   s_scope = ctx;

   if (indexWords && Doxygen::searchIndex) {
      if (md) {
         s_searchUrl = md->getOutputFileBase();
         Doxygen::searchIndex->setCurrentDoc(md, md->anchor(), false);

      } else if (ctx) {
         s_searchUrl = ctx->getOutputFileBase();
         Doxygen::searchIndex->setCurrentDoc(ctx, ctx->anchor(), false);

      }
   }

#if 0
   if (indexWords && md && Doxygen::searchIndex) {
      s_searchUrl = md->getOutputFileBase();
      Doxygen::searchIndex->setCurrentDoc(
         (md->getLanguage() == SrcLangExt_Fortran ?
          theTranslator->trSubprogram(true, true) :
          theTranslator->trMember(true, true)) + " " + md->qualifiedName(),
         s_searchUrl,
         md->anchor());

   } else if (indexWords && ctx && Doxygen::searchIndex) {
      s_searchUrl = ctx->getOutputFileBase();
      QByteArray name = ctx->qualifiedName();

      SrcLangExt lang = ctx->getLanguage();
      QByteArray sep = getLanguageSpecificSeparator(lang);
      if (sep != "::") {
         name = substitute(name, "::", sep);
      }

      switch (ctx->definitionType()) {
         case Definition::TypePage: {
            PageDef *pd = (PageDef *)ctx;
            if (!pd->title().isEmpty()) {
               name = theTranslator->trPage(true, true) + " " + pd->title();
            } else {
               name = theTranslator->trPage(true, true) + " " + pd->name();
            }
         }
         break;
         case Definition::TypeClass: {
            ClassDef *cd = (ClassDef *)ctx;
            name.prepend(cd->compoundTypeString() + " ");
         }
         break;
         case Definition::TypeNamespace: {
            if (lang == SrcLangExt_Java || lang == SrcLangExt_CSharp) {
               name = theTranslator->trPackage(name);
            } else if (lang == SrcLangExt_Fortran) {
               name.prepend(theTranslator->trModule(true, true) + " ");
            } else {
               name.prepend(theTranslator->trNamespace(true, true) + " ");
            }
         }
         break;
         case Definition::TypeGroup: {
            GroupDef *gd = (GroupDef *)ctx;
            if (gd->groupTitle()) {
               name = theTranslator->trGroup(true, true) + " " + gd->groupTitle();
            } else {
               name.prepend(theTranslator->trGroup(true, true) + " ");
            }
         }
         break;
         default:
            break;
      }
      Doxygen::searchIndex->setCurrentDoc(name, s_searchUrl);
   }
#endif

   else {
      s_searchUrl = "";
   }

   s_fileName = fileName;
   s_relPath = (!linkFromIndex && ctx) ? QByteArray(relativePathToRoot(ctx->getOutputFileBase())) : QByteArray("");
  
   s_memberDef = md;
   s_nodeStack.clear();
   s_styleStack.clear();
   s_initialStyleStack.clear();

   s_inSeeBlock        = false;
   s_xmlComment        = false;
   s_insideHtmlLink    = false;
   s_includeFileText   = "";
   s_includeFileOffset = 0;
   s_includeFileLength = 0;

   s_isExample   = isExample;
   s_exampleName = exampleName;

   s_hasParamCommand  = false;
   s_hasReturnCommand = false;
   
   s_paramsFound.clear();
   s_sectionDict = 0; //sections;

   doctokenizerYYlineno = startLine;
   uint inpLen = qstrlen(input);

   QByteArray inpStr = processCopyDoc(input, inpLen);
   if (inpStr.isEmpty() || inpStr.at(inpStr.length() - 1) != '\n') {
      inpStr += '\n';
   }
  
   doctokenizerYYinit(inpStr, s_fileName);

   // build abstract syntax tree
   DocRoot *root = new DocRoot(md != 0, singleLine);
   root->parse();


   if (Debug::isFlagSet(Debug::PrintTree)) {
      // pretty print the result
      PrintDocVisitor *v = new PrintDocVisitor;
      root->accept(v);
      delete v;
   }

   checkUndocumentedParams();
   detectNoDocumentedParams();

   // TODO: These should be called at the end of the program.
   //doctokenizerYYcleanup();
   //Mappers::cmdMapper->freeInstance();
   //Mappers::htmlTagMapper->freeInstance();

   // restore original parser state
   docParserPopContext();
 
   return root;
}

DocText *validatingParseText(const char *input)
{
   // store parser state so we can re-enter this function if needed
   docParserPushContext();
 
   s_context = "";
   s_fileName = "<parseText>";
   s_relPath = "";
   s_memberDef = 0;
   s_nodeStack.clear();
   s_styleStack.clear();
   s_initialStyleStack.clear();
   s_inSeeBlock = false;
   s_xmlComment = false;
   s_insideHtmlLink = false;
   s_includeFileText = "";
   s_includeFileOffset = 0;
   s_includeFileLength = 0;
   s_isExample = false;
   s_exampleName = "";
   s_hasParamCommand = false;
   s_hasReturnCommand = false;
   
   s_paramsFound.clear();
   s_searchUrl = "";

   DocText *txt = new DocText;

   if (input) {
      doctokenizerYYlineno = 1;
      doctokenizerYYinit(input, s_fileName);

      // build abstract syntax tree
      txt->parse();


      if (Debug::isFlagSet(Debug::PrintTree)) {
         // pretty print the result
         PrintDocVisitor *v = new PrintDocVisitor;
         txt->accept(v);
         delete v;
      }
   }

   // restore original parser state
   docParserPopContext();
   return txt;
}

void docFindSections(const char *input, Definition *d, MemberGroup *mg, const char *fileName)
{
   doctokenizerYYFindSections(input, d, mg, fileName);
}

