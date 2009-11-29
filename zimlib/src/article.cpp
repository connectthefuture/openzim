/*
 * Copyright (C) 2006 Tommi Maekitalo
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#include <zim/article.h>
#include <zim/template.h>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include "log.h"

log_define("zim.article")

namespace zim
{
  const std::string& Article::getMimeType() const
  {
    static const std::string textHtml = "text/html; charset=UTF-8";
    static const std::string textPlain = "text/plain";
    static const std::string textXml = "application/xml";
    static const std::string imageJpeg = "image/jpeg";
    static const std::string imagePng = "image/png";
    static const std::string imageTiff = "image/tiff";
    static const std::string textCss = "text/css";
    static const std::string imageGif = "image/gif";
    static const std::string index = "text/plain";
    static const std::string applicationJavaScript = "application/x-javascript";
    static const std::string imageIcon = "image/x-icon";

    switch (getLibraryMimeType())
    {
      case zimMimeTextHtml:
      case zimMimeTextHtmlTemplate:
        return textHtml;
      case zimMimeTextPlain:
        return textPlain;
      case zimMimeImageJpeg:
        return imageJpeg;
      case zimMimeImagePng:
        return imagePng;
      case zimMimeImageTiff:
        return imageTiff;
      case zimMimeTextCss:
        return textCss;
      case zimMimeImageGif:
        return imageGif;
      case zimMimeIndex:
        return index;
      case zimMimeApplicationJavaScript:
        return applicationJavaScript;
      case zimMimeImageIcon:
        return imageIcon;
      case zimMimeTextXml:
        return textXml;
    }

    return textHtml;
  }

  size_type Article::getArticleSize() const
  {
    Dirent dirent = getDirent();
    return file.getCluster(dirent.getClusterNumber())
               .getBlobSize(dirent.getBlobNumber());
  }

  namespace
  {
    class Ev : public TemplateParser::Event
    {
        std::ostream& out;
        Article& article;
        unsigned maxRecurse;

      public:
        Ev(std::ostream& out_, Article& article_, unsigned maxRecurse_)
          : out(out_),
            article(article_),
            maxRecurse(maxRecurse_)
          { }
        void onData(const std::string& data);
        void onToken(const std::string& token);
        void onLink(char ns, const std::string& title);
    };

    void Ev::onData(const std::string& data)
    {
      out << data;
    }

    void Ev::onToken(const std::string& token)
    {
      log_trace("onToken(\"" << token << "\")");

      if (token == "title")
        out << article.getTitle();
      else if (token == "url")
        out << article.getUrl();
      else if (token == "namespace")
        out << article.getNamespace();
      else if (token == "content")
      {
        if (maxRecurse <= 0)
          throw std::runtime_error("maximum recursive limit is reached");
        article.getPage(out, false, maxRecurse - 1);
      }
      else
      {
        log_warn("unknown token \"" << token  << "\" found in template");
        out << "<%" << token << "%>";
      }
    }

    void Ev::onLink(char ns, const std::string& url)
    {
      if (maxRecurse <= 0)
        throw std::runtime_error("maximum recursive limit is reached");
      article.getFile().getArticleByUrl(ns, url).getPage(out, false, maxRecurse - 1);
    }

  }

  std::string Article::getPage(bool layout, unsigned maxRecurse)
  {
    std::ostringstream s;
    getPage(s, layout, maxRecurse);
    return s.str();
  }

  void Article::getPage(std::ostream& out, bool layout, unsigned maxRecurse)
  {
    log_trace("Article::getPage(" << layout << ", " << maxRecurse << ')');

    if (getLibraryMimeType() == zimMimeTextHtml || getLibraryMimeType() == zimMimeTextHtmlTemplate)
    {
      if (layout && file.getFileheader().hasLayoutPage())
      {
        Article layoutPage = file.getArticle(file.getFileheader().getLayoutPage());
        Blob data = layoutPage.getData();

        Ev ev(out, *this, maxRecurse);
        log_debug("call template parser");
        TemplateParser parser(&ev);
        for (const char* p = data.data(); p != data.end(); ++p)
          parser.parse(*p);
        parser.flush();

        return;
      }
      else if (getLibraryMimeType() == zimMimeTextHtmlTemplate)
      {
        Blob data = getData();

        Ev ev(out, *this, maxRecurse);
        TemplateParser parser(&ev);
        for (const char* p = data.data(); p != data.end(); ++p)
          parser.parse(*p);
        parser.flush();

        return;
      }
    }

    // default case - template cases has return above
    out << getData();
  }

}
