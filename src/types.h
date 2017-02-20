/*************************************************************************
 *
 * Copyright (C) 2014-2017 Barbara Geller & Ansel Sermersheim 
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

#ifndef TYPES_H
#define TYPES_H

/** @file
 *  @brief This file contains a number of basic enums and types.
 */

/** Protection level of members */
enum Protection   { Public, Protected, Private, Package };

/** Virtualness of a member */
enum Specifier    { Normal, Virtual, Pure };

/** Reference qualifier for a mehtod */
enum RefType      { NoRef, LValueRef, RValueRef };

/** Kind of method */
enum MethodTypes  { Method, Signal, Slot, DCOP, Property, Event };

/** Type of member relation */
enum RelatesType  { Simple, Duplicate, MemberOf };

/** Kind of member relationship */
enum Relationship { Member, Related, Foreign };

/** Language as given by extension */
enum SrcLangExt {
   SrcLangExt_Unknown  = 0x00000,
   SrcLangExt_IDL      = 0x00008,
   SrcLangExt_Java     = 0x00010,
   SrcLangExt_CSharp   = 0x00020,
   SrcLangExt_D        = 0x00040,
   SrcLangExt_PHP      = 0x00080,
   SrcLangExt_ObjC     = 0x00100,
   SrcLangExt_Cpp      = 0x00200,
   SrcLangExt_JS       = 0x00400,
   SrcLangExt_Python   = 0x00800,
   SrcLangExt_Fortran  = 0x01000,   
   SrcLangExt_XML      = 0x04000,
   SrcLangExt_Tcl      = 0x08000,
   SrcLangExt_Markdown = 0x10000, 
   SrcLangExt_Make     = 0x20000, 
   SrcLangExt_Docs     = 0x40000,
};

/** The various compound types */
enum class CompoundType { Class,     // Entry::CLASS_SEC,
                          Struct,    // Entry::STRUCT_SEC,
                          Union,     // Entry::UNION_SEC,
                          Interface, // Entry::INTERFACE_SEC,
                          Protocol,  // Entry::PROTOCOL_SEC,
                          Category,  // Entry::CATEGORY_SEC,
                          Exception, // Entry::EXCEPTION_SEC
                          Service,   // Entry::CLASS_SEC
                          Singleton, // Entry::CLASS_SEC
                  };

/** Grouping info */
struct Grouping {

   /** Grouping priority */
   enum GroupPri_t {
      GROUPING_LOWEST,
      GROUPING_AUTO_WEAK = GROUPING_LOWEST,     //!< membership in group was defined via \@weakgroup
      GROUPING_AUTO_ADD,     //!< membership in group was defined via \@add[to]group
      GROUPING_AUTO_DEF,     //!< membership in group was defined via \@defgroup
      GROUPING_AUTO_HIGHEST = GROUPING_AUTO_DEF,
      GROUPING_INGROUP,      //!< membership in group was defined by \@ingroup
      GROUPING_HIGHEST = GROUPING_INGROUP
   };

   static QString getGroupPriName( GroupPri_t priority ) {
      switch ( priority ) {
         case GROUPING_AUTO_WEAK:
            return "@weakgroup";

         case GROUPING_AUTO_ADD:
            return "@addtogroup";

         case GROUPING_AUTO_DEF:
            return "@defgroup";

         case GROUPING_INGROUP:
            return "@ingroup";
      }
      return "???";
   }

   Grouping( const QString &gn, GroupPri_t p ) : groupname(gn), pri(p) {}
   Grouping( const Grouping &g ) : groupname(g.groupname), pri(g.pri) {}

   QString groupname;      // name of the group
   GroupPri_t pri;         // priority of this definition

};

struct ListItemInfo {
   QString type;
   int itemId;
};

enum MemberListType {  
   MemberListType_detailedLists      = 0x1000,
   MemberListType_declarationLists   = 0x2000,
   MemberListType_documentationLists = 0x4000,

   MemberListType_pubMethods              = 0,
   MemberListType_proMethods              = 1,
   MemberListType_pacMethods              = 2,
   MemberListType_priMethods              = 3,
   MemberListType_pubStaticMethods        = 4,
   MemberListType_proStaticMethods        = 5,
   MemberListType_pacStaticMethods        = 6,
   MemberListType_priStaticMethods        = 7,

   MemberListType_pubSignals              = 8,
   MemberListType_proSignals              = 9,                                        
   MemberListType_priSignals              = 10,
   MemberListType_pubSlots                = 11,
   MemberListType_proSlots                = 12,
   MemberListType_priSlots                = 13,
  
   MemberListType_pubAttribs              = 14,
   MemberListType_proAttribs              = 15,
   MemberListType_pacAttribs              = 16,
   MemberListType_priAttribs              = 17,
   MemberListType_pubStaticAttribs        = 18,
   MemberListType_proStaticAttribs        = 19,
   MemberListType_pacStaticAttribs        = 20,
   MemberListType_priStaticAttribs        = 21,

   MemberListType_pubTypes                = 22,
   MemberListType_proTypes                = 23,
   MemberListType_pacTypes                = 24,
   MemberListType_priTypes                = 25,

   MemberListType_pubTypedefs             = 26,
   MemberListType_proTypedefs             = 27,
   MemberListType_pacTypedefs             = 28,
   MemberListType_priTypedefs             = 29,

   MemberListType_services                = 30,
   MemberListType_interfaces              = 31,
   MemberListType_properties              = 32,
   MemberListType_events                  = 33,
   MemberListType_friends                 = 34,
   MemberListType_related                 = 35,
   
   MemberListType_dcopMethods             = 36,
    
   MemberListType_typedefMembers          = 37 + MemberListType_detailedLists,
   MemberListType_enumMembers             = 38 + MemberListType_detailedLists,
   MemberListType_enumValMembers          = 39 + MemberListType_detailedLists,
   MemberListType_functionMembers         = 40 + MemberListType_detailedLists,
   MemberListType_relatedMembers          = 41 + MemberListType_detailedLists,
   MemberListType_variableMembers         = 42 + MemberListType_detailedLists,
   MemberListType_propertyMembers         = 43 + MemberListType_detailedLists,
   MemberListType_eventMembers            = 44 + MemberListType_detailedLists,
   MemberListType_constructors            = 45 + MemberListType_detailedLists,
   MemberListType_allMembersList          = 46,

   //
   MemberListType_decDefineMembers        = 47 + MemberListType_declarationLists,
   MemberListType_decProtoMembers         = 48 + MemberListType_declarationLists,
   MemberListType_decTypedefMembers       = 49 + MemberListType_declarationLists,
   MemberListType_decEnumMembers          = 50 + MemberListType_declarationLists,
   MemberListType_decFuncMembers          = 51 + MemberListType_declarationLists,
   MemberListType_decVarMembers           = 52 + MemberListType_declarationLists,
   MemberListType_decEnumValMembers       = 53 + MemberListType_declarationLists,

   MemberListType_decPubSlotMembers       = 54 + MemberListType_declarationLists,
   MemberListType_decProSlotMembers       = 55 + MemberListType_declarationLists,
   MemberListType_decPriSlotMembers       = 56 + MemberListType_declarationLists,
   MemberListType_decPubSignalMembers     = 57 + MemberListType_declarationLists,
   MemberListType_decProSignalMembers     = 58 + MemberListType_declarationLists,   
   MemberListType_decPriSignalMembers     = 59 + MemberListType_declarationLists,

   MemberListType_decEventMembers         = 60 + MemberListType_declarationLists,
   MemberListType_decFriendMembers        = 61 + MemberListType_declarationLists,
   MemberListType_decPropMembers          = 62 + MemberListType_declarationLists,

   //
   MemberListType_docDefineMembers        = 63 + MemberListType_documentationLists,
   MemberListType_docProtoMembers         = 64 + MemberListType_documentationLists,
   MemberListType_docTypedefMembers       = 65 + MemberListType_documentationLists,
   MemberListType_docEnumMembers          = 66 + MemberListType_documentationLists,
   MemberListType_docFuncMembers          = 67 + MemberListType_documentationLists,
   MemberListType_docVarMembers           = 68 + MemberListType_documentationLists,
   MemberListType_docEnumValMembers       = 69 + MemberListType_documentationLists,

   MemberListType_docPubSlotMembers       = 70 + MemberListType_documentationLists,
   MemberListType_docProSlotMembers       = 71 + MemberListType_documentationLists,
   MemberListType_docPriSlotMembers       = 72 + MemberListType_documentationLists,
   MemberListType_docPubSignalMembers     = 73 + MemberListType_documentationLists,
   MemberListType_docProSignalMembers     = 74 + MemberListType_documentationLists,
   MemberListType_docPriSignalMembers     = 75 + MemberListType_documentationLists,

   MemberListType_docEventMembers         = 76 + MemberListType_documentationLists,
   MemberListType_docFriendMembers        = 77 + MemberListType_documentationLists,
   MemberListType_docPropMembers          = 78 + MemberListType_documentationLists,

   MemberListType_redefinedBy             = 79,
   MemberListType_enumFields              = 80,
   MemberListType_memberGroup             = 81,
  
   // this one is for the detailed section on the class page
   MemberListType_interfaceMembers        = 82 + MemberListType_detailedLists,   
   MemberListType_serviceMembers          = 83 + MemberListType_detailedLists,
};

enum MemberType {
   MemberType_Define,
   MemberType_Function,
   MemberType_Variable,
   MemberType_Typedef,
   MemberType_Enumeration,
   MemberType_EnumValue,
   MemberType_Signal,
   MemberType_Slot,
   MemberType_Friend,
   MemberType_DCOP,
   MemberType_Property,
   MemberType_Event,
   MemberType_Interface,
   MemberType_Service,
};

enum FortranFormat {
   FortranFormat_Unknown,
   FortranFormat_Free,
   FortranFormat_Fixed
};

#endif
