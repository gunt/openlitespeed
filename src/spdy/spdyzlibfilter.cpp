/*****************************************************************************
*    Open LiteSpeed is an open source HTTP server.                           *
*    Copyright (C) 2013 - 2020  LiteSpeed Technologies, Inc.                 *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
*                                                                            *
*    This program is distributed in the hope that it will be useful,         *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
*    GNU General Public License for more details.                            *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see http://www.gnu.org/licenses/.      *
*****************************************************************************/
#include "spdyzlibfilter.h"
#include "spdydebug.h"

#include <util/autobuf.h>
#include <util/loopbuf.h>

// SPDY 2 dictionary.
// This is just a hacked dictionary to use for shrinking HTTP-like headers.
static const unsigned char s_spdyV2Dictionary[] =
    "optionsgetheadpostputdeletetraceacceptaccept-charsetaccept-encodingaccept-"
    "languageauthorizationexpectfromhostif-modified-sinceif-matchif-none-matchi"
    "f-rangeif-unmodifiedsincemax-forwardsproxy-authorizationrangerefererteuser"
    "-agent10010120020120220320420520630030130230330430530630740040140240340440"
    "5406407408409410411412413414415416417500501502503504505accept-rangesageeta"
    "glocationproxy-authenticatepublicretry-afterservervarywarningwww-authentic"
    "ateallowcontent-basecontent-encodingcache-controlconnectiondatetrailertran"
    "sfer-encodingupgradeviawarningcontent-languagecontent-lengthcontent-locati"
    "oncontent-md5content-rangecontent-typeetagexpireslast-modifiedset-cookieMo"
    "ndayTuesdayWednesdayThursdayFridaySaturdaySundayJanFebMarAprMayJunJulAugSe"
    "pOctNovDecchunkedtext/htmlimage/pngimage/jpgimage/gifapplication/xmlapplic"
    "ation/xhtmltext/plainpublicmax-agecharset=iso-8859-1utf-8gzipdeflateHTTP/1"
    ".1statusversionurl";
//const int kV2DictionarySize = arraysize(kV2Dictionary);

// SPDY 3 dictionary.
static const unsigned char s_spdyV3Dictionary[] =
{
    0x00, 0x00, 0x00, 0x07, 0x6f, 0x70, 0x74, 0x69,  // ....opti
    0x6f, 0x6e, 0x73, 0x00, 0x00, 0x00, 0x04, 0x68,  // ons....h
    0x65, 0x61, 0x64, 0x00, 0x00, 0x00, 0x04, 0x70,  // ead....p
    0x6f, 0x73, 0x74, 0x00, 0x00, 0x00, 0x03, 0x70,  // ost....p
    0x75, 0x74, 0x00, 0x00, 0x00, 0x06, 0x64, 0x65,  // ut....de
    0x6c, 0x65, 0x74, 0x65, 0x00, 0x00, 0x00, 0x05,  // lete....
    0x74, 0x72, 0x61, 0x63, 0x65, 0x00, 0x00, 0x00,  // trace...
    0x06, 0x61, 0x63, 0x63, 0x65, 0x70, 0x74, 0x00,  // .accept.
    0x00, 0x00, 0x0e, 0x61, 0x63, 0x63, 0x65, 0x70,  // ...accep
    0x74, 0x2d, 0x63, 0x68, 0x61, 0x72, 0x73, 0x65,  // t-charse
    0x74, 0x00, 0x00, 0x00, 0x0f, 0x61, 0x63, 0x63,  // t....acc
    0x65, 0x70, 0x74, 0x2d, 0x65, 0x6e, 0x63, 0x6f,  // ept-enco
    0x64, 0x69, 0x6e, 0x67, 0x00, 0x00, 0x00, 0x0f,  // ding....
    0x61, 0x63, 0x63, 0x65, 0x70, 0x74, 0x2d, 0x6c,  // accept-l
    0x61, 0x6e, 0x67, 0x75, 0x61, 0x67, 0x65, 0x00,  // anguage.
    0x00, 0x00, 0x0d, 0x61, 0x63, 0x63, 0x65, 0x70,  // ...accep
    0x74, 0x2d, 0x72, 0x61, 0x6e, 0x67, 0x65, 0x73,  // t-ranges
    0x00, 0x00, 0x00, 0x03, 0x61, 0x67, 0x65, 0x00,  // ....age.
    0x00, 0x00, 0x05, 0x61, 0x6c, 0x6c, 0x6f, 0x77,  // ...allow
    0x00, 0x00, 0x00, 0x0d, 0x61, 0x75, 0x74, 0x68,  // ....auth
    0x6f, 0x72, 0x69, 0x7a, 0x61, 0x74, 0x69, 0x6f,  // orizatio
    0x6e, 0x00, 0x00, 0x00, 0x0d, 0x63, 0x61, 0x63,  // n....cac
    0x68, 0x65, 0x2d, 0x63, 0x6f, 0x6e, 0x74, 0x72,  // he-contr
    0x6f, 0x6c, 0x00, 0x00, 0x00, 0x0a, 0x63, 0x6f,  // ol....co
    0x6e, 0x6e, 0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e,  // nnection
    0x00, 0x00, 0x00, 0x0c, 0x63, 0x6f, 0x6e, 0x74,  // ....cont
    0x65, 0x6e, 0x74, 0x2d, 0x62, 0x61, 0x73, 0x65,  // ent-base
    0x00, 0x00, 0x00, 0x10, 0x63, 0x6f, 0x6e, 0x74,  // ....cont
    0x65, 0x6e, 0x74, 0x2d, 0x65, 0x6e, 0x63, 0x6f,  // ent-enco
    0x64, 0x69, 0x6e, 0x67, 0x00, 0x00, 0x00, 0x10,  // ding....
    0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d,  // content-
    0x6c, 0x61, 0x6e, 0x67, 0x75, 0x61, 0x67, 0x65,  // language
    0x00, 0x00, 0x00, 0x0e, 0x63, 0x6f, 0x6e, 0x74,  // ....cont
    0x65, 0x6e, 0x74, 0x2d, 0x6c, 0x65, 0x6e, 0x67,  // ent-leng
    0x74, 0x68, 0x00, 0x00, 0x00, 0x10, 0x63, 0x6f,  // th....co
    0x6e, 0x74, 0x65, 0x6e, 0x74, 0x2d, 0x6c, 0x6f,  // ntent-lo
    0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x00, 0x00,  // cation..
    0x00, 0x0b, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e,  // ..conten
    0x74, 0x2d, 0x6d, 0x64, 0x35, 0x00, 0x00, 0x00,  // t-md5...
    0x0d, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74,  // .content
    0x2d, 0x72, 0x61, 0x6e, 0x67, 0x65, 0x00, 0x00,  // -range..
    0x00, 0x0c, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e,  // ..conten
    0x74, 0x2d, 0x74, 0x79, 0x70, 0x65, 0x00, 0x00,  // t-type..
    0x00, 0x04, 0x64, 0x61, 0x74, 0x65, 0x00, 0x00,  // ..date..
    0x00, 0x04, 0x65, 0x74, 0x61, 0x67, 0x00, 0x00,  // ..etag..
    0x00, 0x06, 0x65, 0x78, 0x70, 0x65, 0x63, 0x74,  // ..expect
    0x00, 0x00, 0x00, 0x07, 0x65, 0x78, 0x70, 0x69,  // ....expi
    0x72, 0x65, 0x73, 0x00, 0x00, 0x00, 0x04, 0x66,  // res....f
    0x72, 0x6f, 0x6d, 0x00, 0x00, 0x00, 0x04, 0x68,  // rom....h
    0x6f, 0x73, 0x74, 0x00, 0x00, 0x00, 0x08, 0x69,  // ost....i
    0x66, 0x2d, 0x6d, 0x61, 0x74, 0x63, 0x68, 0x00,  // f-match.
    0x00, 0x00, 0x11, 0x69, 0x66, 0x2d, 0x6d, 0x6f,  // ...if-mo
    0x64, 0x69, 0x66, 0x69, 0x65, 0x64, 0x2d, 0x73,  // dified-s
    0x69, 0x6e, 0x63, 0x65, 0x00, 0x00, 0x00, 0x0d,  // ince....
    0x69, 0x66, 0x2d, 0x6e, 0x6f, 0x6e, 0x65, 0x2d,  // if-none-
    0x6d, 0x61, 0x74, 0x63, 0x68, 0x00, 0x00, 0x00,  // match...
    0x08, 0x69, 0x66, 0x2d, 0x72, 0x61, 0x6e, 0x67,  // .if-rang
    0x65, 0x00, 0x00, 0x00, 0x13, 0x69, 0x66, 0x2d,  // e....if-
    0x75, 0x6e, 0x6d, 0x6f, 0x64, 0x69, 0x66, 0x69,  // unmodifi
    0x65, 0x64, 0x2d, 0x73, 0x69, 0x6e, 0x63, 0x65,  // ed-since
    0x00, 0x00, 0x00, 0x0d, 0x6c, 0x61, 0x73, 0x74,  // ....last
    0x2d, 0x6d, 0x6f, 0x64, 0x69, 0x66, 0x69, 0x65,  // -modifie
    0x64, 0x00, 0x00, 0x00, 0x08, 0x6c, 0x6f, 0x63,  // d....loc
    0x61, 0x74, 0x69, 0x6f, 0x6e, 0x00, 0x00, 0x00,  // ation...
    0x0c, 0x6d, 0x61, 0x78, 0x2d, 0x66, 0x6f, 0x72,  // .max-for
    0x77, 0x61, 0x72, 0x64, 0x73, 0x00, 0x00, 0x00,  // wards...
    0x06, 0x70, 0x72, 0x61, 0x67, 0x6d, 0x61, 0x00,  // .pragma.
    0x00, 0x00, 0x12, 0x70, 0x72, 0x6f, 0x78, 0x79,  // ...proxy
    0x2d, 0x61, 0x75, 0x74, 0x68, 0x65, 0x6e, 0x74,  // -authent
    0x69, 0x63, 0x61, 0x74, 0x65, 0x00, 0x00, 0x00,  // icate...
    0x13, 0x70, 0x72, 0x6f, 0x78, 0x79, 0x2d, 0x61,  // .proxy-a
    0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x7a, 0x61,  // uthoriza
    0x74, 0x69, 0x6f, 0x6e, 0x00, 0x00, 0x00, 0x05,  // tion....
    0x72, 0x61, 0x6e, 0x67, 0x65, 0x00, 0x00, 0x00,  // range...
    0x07, 0x72, 0x65, 0x66, 0x65, 0x72, 0x65, 0x72,  // .referer
    0x00, 0x00, 0x00, 0x0b, 0x72, 0x65, 0x74, 0x72,  // ....retr
    0x79, 0x2d, 0x61, 0x66, 0x74, 0x65, 0x72, 0x00,  // y-after.
    0x00, 0x00, 0x06, 0x73, 0x65, 0x72, 0x76, 0x65,  // ...serve
    0x72, 0x00, 0x00, 0x00, 0x02, 0x74, 0x65, 0x00,  // r....te.
    0x00, 0x00, 0x07, 0x74, 0x72, 0x61, 0x69, 0x6c,  // ...trail
    0x65, 0x72, 0x00, 0x00, 0x00, 0x11, 0x74, 0x72,  // er....tr
    0x61, 0x6e, 0x73, 0x66, 0x65, 0x72, 0x2d, 0x65,  // ansfer-e
    0x6e, 0x63, 0x6f, 0x64, 0x69, 0x6e, 0x67, 0x00,  // ncoding.
    0x00, 0x00, 0x07, 0x75, 0x70, 0x67, 0x72, 0x61,  // ...upgra
    0x64, 0x65, 0x00, 0x00, 0x00, 0x0a, 0x75, 0x73,  // de....us
    0x65, 0x72, 0x2d, 0x61, 0x67, 0x65, 0x6e, 0x74,  // er-agent
    0x00, 0x00, 0x00, 0x04, 0x76, 0x61, 0x72, 0x79,  // ....vary
    0x00, 0x00, 0x00, 0x03, 0x76, 0x69, 0x61, 0x00,  // ....via.
    0x00, 0x00, 0x07, 0x77, 0x61, 0x72, 0x6e, 0x69,  // ...warni
    0x6e, 0x67, 0x00, 0x00, 0x00, 0x10, 0x77, 0x77,  // ng....ww
    0x77, 0x2d, 0x61, 0x75, 0x74, 0x68, 0x65, 0x6e,  // w-authen
    0x74, 0x69, 0x63, 0x61, 0x74, 0x65, 0x00, 0x00,  // ticate..
    0x00, 0x06, 0x6d, 0x65, 0x74, 0x68, 0x6f, 0x64,  // ..method
    0x00, 0x00, 0x00, 0x03, 0x67, 0x65, 0x74, 0x00,  // ....get.
    0x00, 0x00, 0x06, 0x73, 0x74, 0x61, 0x74, 0x75,  // ...statu
    0x73, 0x00, 0x00, 0x00, 0x06, 0x32, 0x30, 0x30,  // s....200
    0x20, 0x4f, 0x4b, 0x00, 0x00, 0x00, 0x07, 0x76,  // .OK....v
    0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x00, 0x00,  // ersion..
    0x00, 0x08, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x31,  // ..HTTP.1
    0x2e, 0x31, 0x00, 0x00, 0x00, 0x03, 0x75, 0x72,  // .1....ur
    0x6c, 0x00, 0x00, 0x00, 0x06, 0x70, 0x75, 0x62,  // l....pub
    0x6c, 0x69, 0x63, 0x00, 0x00, 0x00, 0x0a, 0x73,  // lic....s
    0x65, 0x74, 0x2d, 0x63, 0x6f, 0x6f, 0x6b, 0x69,  // et-cooki
    0x65, 0x00, 0x00, 0x00, 0x0a, 0x6b, 0x65, 0x65,  // e....kee
    0x70, 0x2d, 0x61, 0x6c, 0x69, 0x76, 0x65, 0x00,  // p-alive.
    0x00, 0x00, 0x06, 0x6f, 0x72, 0x69, 0x67, 0x69,  // ...origi
    0x6e, 0x31, 0x30, 0x30, 0x31, 0x30, 0x31, 0x32,  // n1001012
    0x30, 0x31, 0x32, 0x30, 0x32, 0x32, 0x30, 0x35,  // 01202205
    0x32, 0x30, 0x36, 0x33, 0x30, 0x30, 0x33, 0x30,  // 20630030
    0x32, 0x33, 0x30, 0x33, 0x33, 0x30, 0x34, 0x33,  // 23033043
    0x30, 0x35, 0x33, 0x30, 0x36, 0x33, 0x30, 0x37,  // 05306307
    0x34, 0x30, 0x32, 0x34, 0x30, 0x35, 0x34, 0x30,  // 40240540
    0x36, 0x34, 0x30, 0x37, 0x34, 0x30, 0x38, 0x34,  // 64074084
    0x30, 0x39, 0x34, 0x31, 0x30, 0x34, 0x31, 0x31,  // 09410411
    0x34, 0x31, 0x32, 0x34, 0x31, 0x33, 0x34, 0x31,  // 41241341
    0x34, 0x34, 0x31, 0x35, 0x34, 0x31, 0x36, 0x34,  // 44154164
    0x31, 0x37, 0x35, 0x30, 0x32, 0x35, 0x30, 0x34,  // 17502504
    0x35, 0x30, 0x35, 0x32, 0x30, 0x33, 0x20, 0x4e,  // 505203.N
    0x6f, 0x6e, 0x2d, 0x41, 0x75, 0x74, 0x68, 0x6f,  // on-Autho
    0x72, 0x69, 0x74, 0x61, 0x74, 0x69, 0x76, 0x65,  // ritative
    0x20, 0x49, 0x6e, 0x66, 0x6f, 0x72, 0x6d, 0x61,  // .Informa
    0x74, 0x69, 0x6f, 0x6e, 0x32, 0x30, 0x34, 0x20,  // tion204.
    0x4e, 0x6f, 0x20, 0x43, 0x6f, 0x6e, 0x74, 0x65,  // No.Conte
    0x6e, 0x74, 0x33, 0x30, 0x31, 0x20, 0x4d, 0x6f,  // nt301.Mo
    0x76, 0x65, 0x64, 0x20, 0x50, 0x65, 0x72, 0x6d,  // ved.Perm
    0x61, 0x6e, 0x65, 0x6e, 0x74, 0x6c, 0x79, 0x34,  // anently4
    0x30, 0x30, 0x20, 0x42, 0x61, 0x64, 0x20, 0x52,  // 00.Bad.R
    0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x34, 0x30,  // equest40
    0x31, 0x20, 0x55, 0x6e, 0x61, 0x75, 0x74, 0x68,  // 1.Unauth
    0x6f, 0x72, 0x69, 0x7a, 0x65, 0x64, 0x34, 0x30,  // orized40
    0x33, 0x20, 0x46, 0x6f, 0x72, 0x62, 0x69, 0x64,  // 3.Forbid
    0x64, 0x65, 0x6e, 0x34, 0x30, 0x34, 0x20, 0x4e,  // den404.N
    0x6f, 0x74, 0x20, 0x46, 0x6f, 0x75, 0x6e, 0x64,  // ot.Found
    0x35, 0x30, 0x30, 0x20, 0x49, 0x6e, 0x74, 0x65,  // 500.Inte
    0x72, 0x6e, 0x61, 0x6c, 0x20, 0x53, 0x65, 0x72,  // rnal.Ser
    0x76, 0x65, 0x72, 0x20, 0x45, 0x72, 0x72, 0x6f,  // ver.Erro
    0x72, 0x35, 0x30, 0x31, 0x20, 0x4e, 0x6f, 0x74,  // r501.Not
    0x20, 0x49, 0x6d, 0x70, 0x6c, 0x65, 0x6d, 0x65,  // .Impleme
    0x6e, 0x74, 0x65, 0x64, 0x35, 0x30, 0x33, 0x20,  // nted503.
    0x53, 0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x20,  // Service.
    0x55, 0x6e, 0x61, 0x76, 0x61, 0x69, 0x6c, 0x61,  // Unavaila
    0x62, 0x6c, 0x65, 0x4a, 0x61, 0x6e, 0x20, 0x46,  // bleJan.F
    0x65, 0x62, 0x20, 0x4d, 0x61, 0x72, 0x20, 0x41,  // eb.Mar.A
    0x70, 0x72, 0x20, 0x4d, 0x61, 0x79, 0x20, 0x4a,  // pr.May.J
    0x75, 0x6e, 0x20, 0x4a, 0x75, 0x6c, 0x20, 0x41,  // un.Jul.A
    0x75, 0x67, 0x20, 0x53, 0x65, 0x70, 0x74, 0x20,  // ug.Sept.
    0x4f, 0x63, 0x74, 0x20, 0x4e, 0x6f, 0x76, 0x20,  // Oct.Nov.
    0x44, 0x65, 0x63, 0x20, 0x30, 0x30, 0x3a, 0x30,  // Dec.00.0
    0x30, 0x3a, 0x30, 0x30, 0x20, 0x4d, 0x6f, 0x6e,  // 0.00.Mon
    0x2c, 0x20, 0x54, 0x75, 0x65, 0x2c, 0x20, 0x57,  // ..Tue..W
    0x65, 0x64, 0x2c, 0x20, 0x54, 0x68, 0x75, 0x2c,  // ed..Thu.
    0x20, 0x46, 0x72, 0x69, 0x2c, 0x20, 0x53, 0x61,  // .Fri..Sa
    0x74, 0x2c, 0x20, 0x53, 0x75, 0x6e, 0x2c, 0x20,  // t..Sun..
    0x47, 0x4d, 0x54, 0x63, 0x68, 0x75, 0x6e, 0x6b,  // GMTchunk
    0x65, 0x64, 0x2c, 0x74, 0x65, 0x78, 0x74, 0x2f,  // ed.text.
    0x68, 0x74, 0x6d, 0x6c, 0x2c, 0x69, 0x6d, 0x61,  // html.ima
    0x67, 0x65, 0x2f, 0x70, 0x6e, 0x67, 0x2c, 0x69,  // ge.png.i
    0x6d, 0x61, 0x67, 0x65, 0x2f, 0x6a, 0x70, 0x67,  // mage.jpg
    0x2c, 0x69, 0x6d, 0x61, 0x67, 0x65, 0x2f, 0x67,  // .image.g
    0x69, 0x66, 0x2c, 0x61, 0x70, 0x70, 0x6c, 0x69,  // if.appli
    0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x2f, 0x78,  // cation.x
    0x6d, 0x6c, 0x2c, 0x61, 0x70, 0x70, 0x6c, 0x69,  // ml.appli
    0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x2f, 0x78,  // cation.x
    0x68, 0x74, 0x6d, 0x6c, 0x2b, 0x78, 0x6d, 0x6c,  // html.xml
    0x2c, 0x74, 0x65, 0x78, 0x74, 0x2f, 0x70, 0x6c,  // .text.pl
    0x61, 0x69, 0x6e, 0x2c, 0x74, 0x65, 0x78, 0x74,  // ain.text
    0x2f, 0x6a, 0x61, 0x76, 0x61, 0x73, 0x63, 0x72,  // .javascr
    0x69, 0x70, 0x74, 0x2c, 0x70, 0x75, 0x62, 0x6c,  // ipt.publ
    0x69, 0x63, 0x70, 0x72, 0x69, 0x76, 0x61, 0x74,  // icprivat
    0x65, 0x6d, 0x61, 0x78, 0x2d, 0x61, 0x67, 0x65,  // emax-age
    0x3d, 0x67, 0x7a, 0x69, 0x70, 0x2c, 0x64, 0x65,  // .gzip.de
    0x66, 0x6c, 0x61, 0x74, 0x65, 0x2c, 0x73, 0x64,  // flate.sd
    0x63, 0x68, 0x63, 0x68, 0x61, 0x72, 0x73, 0x65,  // chcharse
    0x74, 0x3d, 0x75, 0x74, 0x66, 0x2d, 0x38, 0x63,  // t.utf-8c
    0x68, 0x61, 0x72, 0x73, 0x65, 0x74, 0x3d, 0x69,  // harset.i
    0x73, 0x6f, 0x2d, 0x38, 0x38, 0x35, 0x39, 0x2d,  // so-8859-
    0x31, 0x2c, 0x75, 0x74, 0x66, 0x2d, 0x2c, 0x2a,  // 1.utf-..
    0x2c, 0x65, 0x6e, 0x71, 0x3d, 0x30, 0x2e         // .enq.0.
};

static const unsigned char *s_dicts[] = { NULL, s_spdyV2Dictionary, s_spdyV3Dictionary };
static int  s_dictsLen[] =
{ 0, sizeof(s_spdyV2Dictionary), sizeof(s_spdyV3Dictionary) };

SpdyZlibFilter::SpdyZlibFilter()
    : m_iVersion(2)
    , m_isInflator(0)
{
    memset(&m_stream, 0, sizeof(m_stream));
}


int SpdyZlibFilter::init(int isInflator, int verSpdy)
{
    int ret;

    m_isInflator = isInflator;
    assert((uint)verSpdy < sizeof(s_dicts) / sizeof(char *));
    m_iVersion = verSpdy;
    if (isInflator)
    {
        if (m_stream.state)
            ret = inflateReset(&m_stream);
        else
            ret = inflateInit2(&m_stream, 15);
    }
    else
    {
        if (m_stream.state)
            ret = deflateReset(&m_stream);
        else
            ret = deflateInit(&m_stream, -1);

        if (ret != Z_STREAM_ERROR)
            ret = deflateSetDictionary(&m_stream, s_dicts[ m_iVersion ],
                                       s_dictsLen[ m_iVersion ]);
    }

    return ret;
}


SpdyZlibFilter::~SpdyZlibFilter()
{
    release();
}


int SpdyZlibFilter::release()
{
    if (m_isInflator)
        inflateEnd(&m_stream);
    else
        deflateEnd(&m_stream);
    return 0;
}

int SpdyZlibFilter::decompress(char *pSource, uint32_t length,
                               AutoBuf &bufInflate)
{
    int ret;

    m_stream.avail_in = length;
    m_stream.next_in = (unsigned char *)pSource;
    m_stream.avail_out = bufInflate.available();
    m_stream.next_out = (unsigned char *)bufInflate.end();

    while (m_stream.avail_in)
    {
        ret = inflate(&m_stream, Z_SYNC_FLUSH);
        if (ret == Z_STREAM_ERROR)
            return LS_FAIL;

        switch (ret)
        {

        case Z_NEED_DICT:
            /* Setting the dictionary for the SPDY zlib compression. */
            ret = inflateSetDictionary(&m_stream, s_dicts[ m_iVersion ],
                                       s_dictsLen[ m_iVersion ]);

            if (ret != Z_OK)
            {
                inflateEnd(&m_stream);
                return LS_FAIL;
            }
            continue;

        case Z_DATA_ERROR:
            return Z_DATA_ERROR;

        case Z_MEM_ERROR:
            return Z_MEM_ERROR;

        case Z_OK:
            break;

        default:
            return ret;
        }
        bufInflate.used(m_stream.next_out - (unsigned char *)bufInflate.end());
        if (m_stream.avail_out == 0)
        {
            bufInflate.grow(512);
            m_stream.avail_out = bufInflate.available();
            m_stream.next_out = (unsigned char *)bufInflate.end();
        }
        else
            break;
    }
    return bufInflate.size();
}


int SpdyZlibFilter::compress(char *pSource, uint32_t length,
                             LoopBuf *ploopbuf, int flush)
{
    int ret;
    int old_size = ploopbuf->size();
    ploopbuf->guarantee(1024);

    m_stream.avail_in = length;
    m_stream.next_in = (unsigned char *)pSource;
    m_stream.avail_out = ploopbuf->contiguous();
    m_stream.next_out = (unsigned char *)ploopbuf->end();
    while (m_stream.avail_in)
    {
        ret = deflate(&m_stream, flush);

        if ((ret == Z_OK) || (ret == Z_STREAM_END))
        {
            ploopbuf->used((char *)m_stream.next_out - ploopbuf->end());
            if (m_stream.avail_out == 0)
            {
                ploopbuf->guarantee(1024);
                m_stream.avail_out = ploopbuf->contiguous();
                m_stream.next_out = (unsigned char *)ploopbuf->end();
            }
            else
                break;
        }
        else
        {
            if ((ret == Z_BUF_ERROR) && (m_stream.avail_out > 0)
                && (m_stream.avail_in == 0))
                break;
            return ret;
        }
    }
    return ploopbuf->size() - old_size;
}

