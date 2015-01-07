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

#ifndef RESOURCEMGR_H
#define RESOURCEMGR_H

#include <QByteArray>

/** @brief Compiled resource */
struct Resource {
   enum Type { Verbatim, Luminance, LumAlpha, CSS };

   const char *category;
   const char *name;
   const char *data;

   int size;  
};

/** @brief Singleton for managing resources compiled into an executable */
class ResourceMgr
{
 public:
   /** Returns the one and only instance of this class */
   static ResourceMgr &instance();

   /** Registers an array of resources */
   void registerResources(const Resource resources[], int numResources);

   /** Copies all resource belonging to a given category to a given target directory */
   bool copyCategory(const char *categoryName, const char *targetDir) const;
 
   /** Copies a registered resource to a given target directory under a given target name */
   bool copyResourceAs(const QString &fName, const QString &targetDir, const QString &targetName) const;

   /** Returns a pointer to the resource object with the given name. */
   const Resource *get(const char *name) const;

   /** Gets the resource data as a QByteArray */
   QByteArray getAsString(const QString &fName) const;

 private:
   ResourceMgr();
   ~ResourceMgr();

   QHash<QString, const Resource *> m_resources;   
};

#endif
