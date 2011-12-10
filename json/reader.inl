/******************************************************************************

Copyright (c) 2009-2010, Terry Caton
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright 
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the projecct nor the names of its contributors 
      may be used to endorse or promote products derived from this software 
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/

#include <cassert>
#include <set>
#include <sstream>

/*  

TODO:
* better documentation
* unicode character decoding

*/

namespace json
{

inline std::istream& operator >> (std::istream& istr, UnknownElement& elementRoot)
{
   Reader::Read(elementRoot, istr);
   return istr;
}

inline Reader::Location::Location() :
   m_nLine(0),
   m_nLineOffset(0),
   m_nDocOffset(0)
{}


struct Reader::Token
{
   enum Type
   {
      TOKEN_OBJECT_BEGIN,  //    {
      TOKEN_OBJECT_END,    //    }
      TOKEN_ARRAY_BEGIN,   //    [
      TOKEN_ARRAY_END,     //    ]
      TOKEN_NEXT_ELEMENT,  //    ,
      TOKEN_MEMBER_ASSIGN, //    :
      TOKEN_STRING,        //    "xxx"
      TOKEN_NUMBER,        //    [+/-]000.000[e[+/-]000]
      TOKEN_BOOLEAN_TRUE,  //    true
      TOKEN_BOOLEAN_FALSE, //    false
      TOKEN_NULL,          //    null
   };

   Type nType;
   std::string sValue;

   // for malformed file debugging
   Reader::Location locBegin;
   Reader::Location locEnd;
};



//////////////////////
// Reader::InputStream

class Reader::InputStream // would be cool if std::istream had virtual functions to override
{
public:
   InputStream(std::istream& iStr) :
      m_iStr(iStr) {}

   // protect access to the input stream, so we can keeep track of document/line offsets
   char Get();
   char Peek();
   bool EOS();
   void EatWhiteSpace();

   const Location& GetLocation() const { return m_Location; }

private:
   std::istream& m_iStr;
   Location m_Location;
};


inline char Reader::InputStream::Get()
{
   assert(m_iStr.eof() == false); // enforce reading of only valid stream data 
   char c = m_iStr.get();
   
   ++m_Location.m_nDocOffset;
   if (c == '\n') {
      ++m_Location.m_nLine;
      m_Location.m_nLineOffset = 0;
   }
   else {
      ++m_Location.m_nLineOffset;
   }

   return c;
}


inline char Reader::InputStream::Peek()
{
   if (m_iStr.eof()) // enforce reading of only valid stream data 
   {
      std::string sMessage = "Unexpected end of input stream";
      throw ScanException(sMessage, GetLocation()); // nowhere to point to
   }
   return m_iStr.peek();
}


inline bool Reader::InputStream::EOS()
{
   // apparently eof flag isn't set until a character read is attempted. whatever.
   Peek();    
   return m_iStr.eof();
}

inline void Reader::InputStream::EatWhiteSpace()
{
   // need to keep track of line & column offsets, don't use ws
   while (EOS() == false && ::isspace(Peek()))
      Get();
}


//////////////////////
// Reader::Scanner


class Reader::Scanner
{
public:
   Scanner(InputStream& inputStream);

   // scanning istream into token sequence
   Token::Type Peek();
   Token Get();

private:
   std::string MatchString();
   std::string MatchNumber();
   std::string MatchExpectedString(const std::string& sExpected);

   InputStream& m_InputStream;
};


inline Reader::Scanner::Scanner(Reader::InputStream& inputStream) :
   m_InputStream(inputStream)
{}


inline Reader::Token::Type Reader::Scanner::Peek()
{
   m_InputStream.EatWhiteSpace();

   // gives us null-terminated string
   char sChar = m_InputStream.Peek();
   Token::Type nType;
   switch (sChar)
   {
      case '{':      nType = Token::TOKEN_OBJECT_BEGIN;     break;
      case '}':      nType = Token::TOKEN_OBJECT_END;       break;
      case '[':      nType = Token::TOKEN_ARRAY_BEGIN;      break;
      case ']':      nType = Token::TOKEN_ARRAY_END;        break;
      case ',':      nType = Token::TOKEN_NEXT_ELEMENT;     break;
      case ':':      nType = Token::TOKEN_MEMBER_ASSIGN;    break;
      case '"':      nType = Token::TOKEN_STRING;           break;
      case '-':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':      nType = Token::TOKEN_NUMBER;           break;
      case 't':      nType = Token::TOKEN_BOOLEAN_TRUE;     break;
      case 'f':      nType = Token::TOKEN_BOOLEAN_FALSE;    break;
      case 'n':      nType = Token::TOKEN_NULL;             break;
      default:
      {
         std::string sErrorMessage = std::string("Unexpected character in stream: ") + sChar;
         throw ScanException(sErrorMessage, m_InputStream.GetLocation());
      }
   }

   return nType;
}


inline Reader::Token Reader::Scanner::Get()
{
   Token token;
   token.nType = Peek();
   token.locBegin = m_InputStream.GetLocation();

   switch (token.nType)
   {
      case Token::TOKEN_OBJECT_BEGIN:    token.sValue = MatchExpectedString("{");       break;
      case Token::TOKEN_OBJECT_END:      token.sValue = MatchExpectedString("}");       break;
      case Token::TOKEN_ARRAY_BEGIN:     token.sValue = MatchExpectedString("[");       break;
      case Token::TOKEN_ARRAY_END:       token.sValue = MatchExpectedString("]");       break;
      case Token::TOKEN_NEXT_ELEMENT:    token.sValue = MatchExpectedString(",");       break;
      case Token::TOKEN_MEMBER_ASSIGN:   token.sValue = MatchExpectedString(":");       break;
      case Token::TOKEN_STRING:          token.sValue = MatchString();                    break;
      case Token::TOKEN_NUMBER:          token.sValue = MatchNumber();                    break;
      case Token::TOKEN_BOOLEAN_TRUE:    token.sValue = MatchExpectedString("true");    break;
      case Token::TOKEN_BOOLEAN_FALSE:   token.sValue = MatchExpectedString("false");   break;
      case Token::TOKEN_NULL:            token.sValue = MatchExpectedString("null");    break;
      default:                           assert(0);                                                  
   }

   token.locEnd = m_InputStream.GetLocation();
   return token;
}


inline std::string Reader::Scanner::MatchExpectedString(const std::string& sExpected)
{
   std::string::const_iterator it(sExpected.begin()),
                               itEnd(sExpected.end());
   for ( ; it != itEnd; ++it) {
      if (m_InputStream.EOS() ||      // did we reach the end before finding what we're looking for...
          m_InputStream.Get() != *it) // ...or did we find something different?
      {
         std::string sMessage = std::string("Expected string: ") + sExpected;
         throw ScanException(sMessage, m_InputStream.GetLocation());
      }
   }

   // all's well if we made it here
   return sExpected;
}


inline std::string Reader::Scanner::MatchString()
{
   MatchExpectedString("\"");

   std::string string;
   while (m_InputStream.EOS() == false &&
          m_InputStream.Peek() != '"')
   {
      char c = m_InputStream.Get();

      // escape?
      if (c == '\\' &&
          m_InputStream.EOS() == false) // shouldn't have reached the end yet
      {
         c = m_InputStream.Get();
         switch (c) {
            case '/':      string.push_back('/');     break;
            case '"':      string.push_back('"');     break;
            case '\\':     string.push_back('\\');    break;
            case 'b':      string.push_back('\b');    break;
            case 'f':      string.push_back('\f');    break;
            case 'n':      string.push_back('\n');    break;
            case 'r':      string.push_back('\r');    break;
            case 't':      string.push_back('\t');    break;
            case 'u':      string.push_back('\u');    break; // TODO: what do we do with this?
            default: {
               std::string sMessage = std::string("Unrecognized escape sequence found in string: \\") + c;
               throw ScanException(sMessage, m_InputStream.GetLocation());
            }
         }
      }
      else {
         string.push_back(c);
      }
   }

   // eat the last '"' that we just peeked
   MatchExpectedString("\"");

   // all's well if we made it here
   return string;
}


inline std::string Reader::Scanner::MatchNumber()
{
   const char sNumericChars[] = "0123456789.eE-+";
   std::set<char> numericChars;
   numericChars.insert(sNumericChars, sNumericChars + sizeof(sNumericChars));

   std::string sNumber;
   while (m_InputStream.EOS() == false &&
          numericChars.find(m_InputStream.Peek()) != numericChars.end())
   {
      sNumber.push_back(m_InputStream.Get());   
   }

   return sNumber;
}


///////////////////
// Reader::Parser

class Reader::Parser
{
public:
   Parser(Scanner& scanner);

   // parsing token sequence into element structure
   void Parse(UnknownElement& element);
   void Parse(Object& object);
   void Parse(Array& array);
   void Parse(String& string);
   void Parse(Number& number);
   void Parse(Boolean& boolean);
   void Parse(Null& null);

private:
   Token MatchExpectedToken(Token::Type nExpected);

   Reader::Scanner& m_Scanner;
};


inline Reader::Parser::Parser(Reader::Scanner& scanner) :
   m_Scanner(scanner)
{}

inline void Reader::Parser::Parse(UnknownElement& element) 
{
   Token::Type nType = m_Scanner.Peek();
   switch (nType)
   {
      case Token::TOKEN_OBJECT_BEGIN:
      {
         // implicit non-const cast will perform conversion for us (if necessary)
         Object& object = element;
         Parse(object);
         break;
      }

      case Token::TOKEN_ARRAY_BEGIN:
      {
         Array& array = element;
         Parse(array);
         break;
      }

      case Token::TOKEN_STRING:
      {
         String& string = element;
         Parse(string);
         break;
      }

      case Token::TOKEN_NUMBER:
      {
         Number& number = element;
         Parse(number);
         break;
      }

      case Token::TOKEN_BOOLEAN_TRUE:
      case Token::TOKEN_BOOLEAN_FALSE:
      {
         Boolean& boolean = element;
         Parse(boolean);
         break;
      }

      case Token::TOKEN_NULL:
      {
         Null& null = element;
         Parse(null);
         break;
      }

      default:
      {
         // didn't find what we expected. what did we find? extract it & fail
         Token token = m_Scanner.Get();
         std::string sMessage = std::string("Unexpected token: " + token.sValue);
         throw ParseException(sMessage, token.locBegin, token.locEnd);
      }
   }
}


inline void Reader::Parser::Parse(Object& object)
{
   MatchExpectedToken(Token::TOKEN_OBJECT_BEGIN);

   Token::Type nNextTokenType = m_Scanner.Peek();
   while (nNextTokenType != Token::TOKEN_OBJECT_END)
   {
      Object::Member member;

      // first the member name. save the token in case we have to throw an exception
      Token tokenName = MatchExpectedToken(Token::TOKEN_STRING);
      member.name = tokenName.sValue;

      // ...then the key/value separator...
      MatchExpectedToken(Token::TOKEN_MEMBER_ASSIGN);

      // ...then the value itself (can be anything).
      Parse(member.element);

      // try adding it to the object (this could throw)
      try
      {
         object.Insert(member);
      }
      catch (Exception&)
      {
         // must be a duplicate name
         std::string sMessage = std::string("Duplicate object member token: ") + member.name; 
         throw ParseException(sMessage, tokenName.locBegin, tokenName.locEnd);
      }

      nNextTokenType = m_Scanner.Peek();
      if (nNextTokenType == Token::TOKEN_NEXT_ELEMENT)
      {
         MatchExpectedToken(Token::TOKEN_NEXT_ELEMENT);
         nNextTokenType = m_Scanner.Peek();
      }
   }

   MatchExpectedToken(Token::TOKEN_OBJECT_END);
}


inline void Reader::Parser::Parse(Array& array)
{
   MatchExpectedToken(Token::TOKEN_ARRAY_BEGIN);

   Token::Type nNextTokenType = m_Scanner.Peek();
   while (nNextTokenType != Token::TOKEN_ARRAY_END)
   {
      // ...what's next? could be anything
      Array::iterator itElement = array.Insert(UnknownElement());
      Parse(*itElement);

      nNextTokenType = m_Scanner.Peek();
      if (nNextTokenType == Token::TOKEN_NEXT_ELEMENT)
      {
         MatchExpectedToken(Token::TOKEN_NEXT_ELEMENT);
         nNextTokenType = m_Scanner.Peek();
      }
   }

   MatchExpectedToken(Token::TOKEN_ARRAY_END);
}


inline void Reader::Parser::Parse(String& string)
{
   Token token = MatchExpectedToken(Token::TOKEN_STRING);
   string = token.sValue;
}


inline void Reader::Parser::Parse(Number& number)
{
   Token token = MatchExpectedToken(Token::TOKEN_NUMBER);
   
   std::istringstream iStr(token.sValue);
   double dValue;
   iStr >> dValue;

   // did we consume all characters in the token?
   if (iStr.eof() == false)
   {
      char c = iStr.peek();
      std::string sMessage = std::string("Unexpected character in NUMBER token: ") + c;
      throw ParseException(sMessage, token.locBegin, token.locEnd);
   }

   number = dValue;
}


inline void Reader::Parser::Parse(Boolean& boolean)
{
   Token::Type nType = m_Scanner.Peek();
   assert(nType == Token::TOKEN_BOOLEAN_TRUE ||
          nType == Token::TOKEN_BOOLEAN_FALSE);
   Token token = MatchExpectedToken(nType);
   boolean = (token.sValue == "true" ? true : false);
}


inline void Reader::Parser::Parse(Null&)
{
   MatchExpectedToken(Token::TOKEN_NULL);
}


inline Reader::Token Reader::Parser::MatchExpectedToken(Token::Type nExpected)
{
   // get the next token, whatever it is, that way we have location info. we'll sanity check the type later
   Token token = m_Scanner.Get();
   if (token.nType != nExpected)
   {
      // didn't find what we expected. what did we find? extract it & fail
      std::string sMessage = std::string("Unexpected token: ") + token.sValue;
      throw ParseException(sMessage, token.locBegin, token.locEnd);
   }

   return token;
}




///////////////////
// Reader (finally)


inline void Reader::Read(Object& object, std::istream& istr)                { Read_i(object, istr); }
inline void Reader::Read(Array& array, std::istream& istr)                  { Read_i(array, istr); }
inline void Reader::Read(String& string, std::istream& istr)                { Read_i(string, istr); }
inline void Reader::Read(Number& number, std::istream& istr)                { Read_i(number, istr); }
inline void Reader::Read(Boolean& boolean, std::istream& istr)              { Read_i(boolean, istr); }
inline void Reader::Read(Null& null, std::istream& istr)                    { Read_i(null, istr); }
inline void Reader::Read(UnknownElement& unknown, std::istream& istr)       { Read_i(unknown, istr); }


template <typename ElementTypeT>   
void Reader::Read_i(ElementTypeT& element, std::istream& istr)
{
   InputStream inputStream(istr);
   Scanner scanner(inputStream);
   Parser parser(scanner);
   
   parser.Parse(element);
}


} // End namespace
