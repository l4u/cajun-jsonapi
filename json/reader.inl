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

inline std::istream& operator >> (std::istream& istr, UnknownElement& elementRoot) {
   Reader::Read(elementRoot, istr);
   return istr;
}

inline Reader::Location::Location() :
   m_nLine(0),
   m_nLineOffset(0),
   m_nDocOffset(0)
{}


//////////////////////
// Reader::InputStream

class Reader::InputStream // would be cool if we could inherit from std::istream & override "get"
{
public:
   InputStream(std::istream& iStr) :
      m_iStr(iStr) {}

   // protect access to the input stream, so we can keeep track of document/line offsets
   char Get(); // big, define outside
   char Peek() {
      if (m_iStr.eof()) // enforce reading of only valid stream data 
      {
         std::string sMessage = "Unexpected end of input stream";
         throw ScanException(sMessage, GetLocation()); // nowhere to point to
      }
      return m_iStr.peek();
   }

   bool EOS() {
      m_iStr.peek(); // apparently eof flag isn't set until a character read is attempted. whatever.
      // TODO: throw if EOF
      return m_iStr.eof();
   }

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
   Reader reader;

   InputStream inputStream(istr);
   reader.Parse(element, inputStream);
}


inline Reader::Token::Type Reader::Peek(InputStream& inputStream)
{
   EatWhiteSpace(inputStream);

   // gives us null-terminated string
   char sChar = inputStream.Peek();
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
         throw ScanException(sErrorMessage, inputStream.GetLocation());
      }
   }

   return nType;
}


inline Reader::Token Reader::GetNextToken(InputStream& inputStream)
{
   Token token;
   token.nType = Peek(inputStream);
   token.locBegin = inputStream.GetLocation();

   switch (token.nType)
   {
      case Token::TOKEN_OBJECT_BEGIN:    token.sValue = MatchExpectedString(inputStream, "{");       break;
      case Token::TOKEN_OBJECT_END:      token.sValue = MatchExpectedString(inputStream, "}");       break;
      case Token::TOKEN_ARRAY_BEGIN:     token.sValue = MatchExpectedString(inputStream, "[");       break;
      case Token::TOKEN_ARRAY_END:       token.sValue = MatchExpectedString(inputStream, "]");       break;
      case Token::TOKEN_NEXT_ELEMENT:    token.sValue = MatchExpectedString(inputStream, ",");       break;
      case Token::TOKEN_MEMBER_ASSIGN:   token.sValue = MatchExpectedString(inputStream, ":");       break;
      case Token::TOKEN_STRING:          token.sValue = MatchString(inputStream);                    break;
      case Token::TOKEN_NUMBER:          token.sValue = MatchNumber(inputStream);                    break;
      case Token::TOKEN_BOOLEAN_TRUE:    token.sValue = MatchExpectedString(inputStream, "true");    break;
      case Token::TOKEN_BOOLEAN_FALSE:   token.sValue = MatchExpectedString(inputStream, "false");   break;
      case Token::TOKEN_NULL:            token.sValue = MatchExpectedString(inputStream, "null");    break;
      default:                           assert(0);                                                  
   }

   token.locEnd = inputStream.GetLocation();
   return token;
}

inline void Reader::EatWhiteSpace(InputStream& inputStream)
{
   while (inputStream.EOS() == false && 
          ::isspace(inputStream.Peek()))
      inputStream.Get();
}


inline std::string Reader::MatchExpectedString(InputStream& inputStream, const std::string& sExpected)
{
   std::string::const_iterator it(sExpected.begin()),
                               itEnd(sExpected.end());
   for ( ; it != itEnd; ++it) {
      if (inputStream.EOS() ||      // did we reach the end before finding what we're looking for...
          inputStream.Get() != *it) // ...or did we find something different?
      {
         std::string sMessage = std::string("Expected string: ") + sExpected;
         throw ScanException(sMessage, inputStream.GetLocation());
      }
   }

   // all's well if we made it here
   return sExpected;
}


inline std::string Reader::MatchString(InputStream& inputStream)
{
   MatchExpectedString(inputStream, "\"");

   std::string string;
   while (inputStream.EOS() == false &&
          inputStream.Peek() != '"')
   {
      char c = inputStream.Get();

      // escape?
      if (c == '\\' &&
          inputStream.EOS() == false) // shouldn't have reached the end yet
      {
         c = inputStream.Get();
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
               throw ScanException(sMessage, inputStream.GetLocation());
            }
         }
      }
      else {
         string.push_back(c);
      }
   }

   // eat the last '"' that we just peeked
   MatchExpectedString(inputStream, "\"");

   // all's well if we made it here
   return string;
}


inline std::string Reader::MatchNumber(InputStream& inputStream)
{
   const char sNumericChars[] = "0123456789.eE-+";
   std::set<char> numericChars;
   numericChars.insert(sNumericChars, sNumericChars + sizeof(sNumericChars));

   std::string sNumber;
   while (inputStream.EOS() == false &&
          numericChars.find(inputStream.Peek()) != numericChars.end())
   {
      sNumber.push_back(inputStream.Get());   
   }

   return sNumber;
}


inline void Reader::Parse(UnknownElement& element, InputStream& inputStream) 
{
   Token::Type nType = Peek(inputStream);
   switch (nType)
   {
      case Token::TOKEN_OBJECT_BEGIN:
      {
         // implicit non-const cast will perform conversion for us (if necessary)
         Object& object = element;
         Parse(object, inputStream);
         break;
      }

      case Token::TOKEN_ARRAY_BEGIN:
      {
         Array& array = element;
         Parse(array, inputStream);
         break;
      }

      case Token::TOKEN_STRING:
      {
         String& string = element;
         Parse(string, inputStream);
         break;
      }

      case Token::TOKEN_NUMBER:
      {
         Number& number = element;
         Parse(number, inputStream);
         break;
      }

      case Token::TOKEN_BOOLEAN_TRUE:
      case Token::TOKEN_BOOLEAN_FALSE:
      {
         Boolean& boolean = element;
         Parse(boolean, inputStream);
         break;
      }

      case Token::TOKEN_NULL:
      {
         Null& null = element;
         Parse(null, inputStream);
         break;
      }

      default:
      {
         // didn't find what we expected. what did we find? extract it & fail
         Token token = GetNextToken(inputStream);
         std::string sMessage = std::string("Unexpected token: " + token.sValue);
         throw ParseException(sMessage, token.locBegin, token.locEnd);
      }
   }
}


inline void Reader::Parse(Object& object, Reader::InputStream& inputStream)
{
   MatchExpectedToken(Token::TOKEN_OBJECT_BEGIN, inputStream);

   Token::Type nNextTokenType = Peek(inputStream);
   while (nNextTokenType != Token::TOKEN_OBJECT_END)
   {
      Object::Member member;

      // first the member name. save the token in case we have to throw an exception
      Token tokenName = MatchExpectedToken(Token::TOKEN_STRING, inputStream);
      member.name = tokenName.sValue;

      // ...then the key/value separator...
      MatchExpectedToken(Token::TOKEN_MEMBER_ASSIGN, inputStream);

      // ...then the value itself (can be anything).
      Parse(member.element, inputStream);

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

      nNextTokenType = Peek(inputStream);
      if (nNextTokenType == Token::TOKEN_NEXT_ELEMENT)
      {
         MatchExpectedToken(Token::TOKEN_NEXT_ELEMENT, inputStream);
         nNextTokenType = Peek(inputStream);
      }
   }

   MatchExpectedToken(Token::TOKEN_OBJECT_END, inputStream);
}


inline void Reader::Parse(Array& array, Reader::InputStream& inputStream)
{
   MatchExpectedToken(Token::TOKEN_ARRAY_BEGIN, inputStream);

   Token::Type nNextTokenType = Peek(inputStream);
   while (nNextTokenType != Token::TOKEN_ARRAY_END)
   {
      // ...what's next? could be anything
      Array::iterator itElement = array.Insert(UnknownElement());
      Parse(*itElement, inputStream);

      nNextTokenType = Peek(inputStream);
      if (nNextTokenType == Token::TOKEN_NEXT_ELEMENT)
      {
         MatchExpectedToken(Token::TOKEN_NEXT_ELEMENT, inputStream);
         nNextTokenType = Peek(inputStream);
      }
   }

   MatchExpectedToken(Token::TOKEN_ARRAY_END, inputStream);
}


inline void Reader::Parse(String& string, Reader::InputStream& inputStream)
{
   Token token = MatchExpectedToken(Token::TOKEN_STRING, inputStream);
   string = token.sValue;
}


inline void Reader::Parse(Number& number, Reader::InputStream& inputStream)
{
   Token token = MatchExpectedToken(Token::TOKEN_NUMBER, inputStream);
   
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


inline void Reader::Parse(Boolean& boolean, Reader::InputStream& inputStream)
{
   Token::Type nType = Peek(inputStream);
   assert(nType == Token::TOKEN_BOOLEAN_TRUE ||
          nType == Token::TOKEN_BOOLEAN_FALSE);
   Token token = MatchExpectedToken(nType, inputStream);
   boolean = (token.sValue == "true" ? true : false);
}


inline void Reader::Parse(Null&, Reader::InputStream& inputStream)
{
   MatchExpectedToken(Token::TOKEN_NULL, inputStream);
}


inline Reader::Token Reader::MatchExpectedToken(Token::Type nExpected, InputStream& inputStream)
{
   EatWhiteSpace(inputStream);

   // get the next token, whatever it is, that way we have location info. we'll sanity check the type later
   Token token = GetNextToken(inputStream);
   if (token.nType != nExpected)
   {
      // didn't find what we expected. what did we find? extract it & fail
      std::string sMessage = std::string("Unexpected token: ") + token.sValue;
      throw ParseException(sMessage, token.locBegin, token.locEnd);
   }

   return token;
}

} // End namespace
