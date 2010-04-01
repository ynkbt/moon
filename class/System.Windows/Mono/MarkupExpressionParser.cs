//
// MarkupExpressionParser.cs
//
// Contact:
//   Moonlight List (moonlight-list@lists.ximian.com)
//
// Copyright 2009 Novell, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

using System;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Data;
using System.Collections.Generic;
using System.Windows.Markup;


namespace Mono.Xaml {

	internal sealed class MarkupExpressionParser {

		private bool parsingBinding;
		StringBuilder piece;
		private object target;
		private string attribute_name;
		private IntPtr parser;
		private IntPtr target_data;

		public MarkupExpressionParser (object target, string attribute_name, IntPtr parser, IntPtr target_data)
		{
			this.target = target;
			this.attribute_name = attribute_name;
			this.parser = parser;
			this.target_data = target_data;
		}
#if __TESTING
		public static void Main ()
		{
			TestParseBinding ("{Binding}");
			TestParseBinding ("{Binding MyProperty}");
			TestParseBinding ("{Binding Fill, Mode=OneWay}");
			TestParseBinding ("{Binding Source={StaticResource Brush}}");
			TestParseBinding ("{Binding Width, Path=Height, Source={StaticResource rect}, Mode=OneTime, Path=RadiusX}");
			TestParseBinding ("{Binding OpacityString, Source={StaticResource CLRObject}, Mode=OneTime}");

			Console.WriteLine ("done with tests");
		}

		public static void TestParseBinding (string exp)
		{
			MarkupExpressionParser p = new MarkupExpressionParser (null, String.Empty, IntPtr.Zero);

			try {
				p.ParseExpression (ref exp);
			} catch (Exception e) {
				Console.WriteLine ("exception while parsing:  {0}", exp);
				Console.WriteLine (e);
			}
		}
#endif

		public static bool IsTemplateBinding (string expression)
		{
			return MatchExpression ("TemplateBinding", expression);
		}

		public static bool IsStaticResource (string expression)
		{
			return MatchExpression ("StaticResource", expression);
		}

		public static bool IsBinding (string expression)
		{
			return MatchExpression ("Binding", expression);
		}

		private delegate object ExpressionHandler (ref string expression);

		public object ParseExpression (ref string expression)
		{
			if (expression.StartsWith ("{}"))
				return expression.Substring (2);

			object result = null;
			bool rv = false;

			if (!rv)
				rv = TryHandler ("Binding", ParseBinding, ref expression, out result);
			if (!rv)
				rv = TryHandler ("StaticResource", ParseStaticResource, ref expression, out result);
			if (!rv)
				rv = TryHandler ("TemplateBinding", ParseTemplateBinding, ref expression, out result);
			if (!rv)
				rv = TryHandler ("RelativeSource", ParseRelativeSource, ref expression, out result);

			return result;
		}

		private static bool MatchExpression (string match, string expression)
		{
			int dummy;
			return MatchExpression (match, expression, out dummy);
		}

		private static bool MatchExpression (string match, string expression, out int end)
		{
			if (expression.Length < 2) {
				end = 1;
				return false;
			}

			if (expression [0] != '{') {
				end = 2;
				return false;
			}

			int i;
			bool found = false;
			for (i = 1; i < expression.Length; i++) {
				if (expression [i] == ' ')
					continue;
				found = true;
				break;
			}

			if (!found) {
				end = 3;
				return false;
			}

			if (i + match.Length > expression.Length) {
				end = 4;
				return false;
			}
				
			int c;
			for (c = 0; c < match.Length; c++) {
				if (expression [i+c] == match [c])
					continue;
				end = 5;
				return false;
			}

			if (c != match.Length) {
				end = 6;
				return false;
			}

			end = i + c;
			return true;
		}

		private bool TryHandler (string match, ExpressionHandler handler, ref string expression, out object result)
		{
			int len;
			if (!MatchExpression (match, expression, out len)) {
				result = null;
				return false;
			}

			expression = expression.Substring (len);
			result = handler (ref expression);
			return true;
		}

		public Binding ParseBinding (ref string expression)
		{
			Binding binding = new Binding ();
			parsingBinding  = true;
			char next;

			if (expression [0] == '}')
				return binding;

			string remaining = expression;
			string piece = GetNextPiece (ref remaining, out next);
			

			if (next == '=')
				HandleProperty (binding, piece, ref remaining);
			else
				binding.Path = new PropertyPath (piece);

			do {
				piece = GetNextPiece (ref remaining, out next);

				if (piece == null)
					break;

				HandleProperty (binding, piece, ref remaining);
			} while (true);

			parsingBinding = false;
			return binding;
		}

		public object ParseStaticResource (ref string expression)
		{
			char next;
			string name = GetNextPiece (ref expression, out next);

			object o = LookupNamedResource (null, name);

#if !__TESTING
			if (o == null)
				o = Application.Current.Resources [name];
#endif

			return o;
		}

		public object ParseTemplateBinding (ref string expression)
		{
			TemplateBindingExpression tb = new TemplateBindingExpression ();

			char next;
			string prop = GetNextPiece (ref expression, out next);
			/*FrameworkTemplate template = */GetParentTemplate ();

			tb.Target = target as FrameworkElement;
			tb.TargetPropertyName = attribute_name;
			tb.SourcePropertyName = prop;
			// tb.Source will be filled in elsewhere between attaching the change handler.

			return tb;
		}

		public object ParseRelativeSource (ref string expression)
		{
			char next;
			string mode_str = GetNextPiece (ref expression, out next);

			if (!Enum.IsDefined (typeof (RelativeSourceMode), mode_str))
				throw new XamlParseException (String.Format ("MarkupExpressionParser:  Error parsing RelativeSource, unknown mode: {0}", mode_str));
				
			return new RelativeSource ((RelativeSourceMode) Enum.Parse (typeof (RelativeSourceMode), mode_str, true));
		}

		private object LookupNamedResource (DependencyObject dob, string name)
		{
			if (name == null)
				throw new XamlParseException ("you must specify a key in {StaticResource}");

			IntPtr value_ptr = NativeMethods.xaml_lookup_named_item (parser, target_data, name);
			object o = Value.ToObject (null, value_ptr);
			if (value_ptr != IntPtr.Zero)
				NativeMethods.value_free_value2 (value_ptr);

			if (o == null && !parsingBinding)
				throw new XamlParseException (String.Format ("Resource '{0}' must be available as a static resource", name));
			return o;
		}

		private FrameworkTemplate GetParentTemplate ()
		{
			IntPtr template = NativeMethods.xaml_get_template_parent (parser, target_data);

			if (template == IntPtr.Zero)
				return null;

			INativeEventObjectWrapper dob = NativeDependencyObjectHelper.FromIntPtr (template);

			return dob as FrameworkTemplate;
		}

		private void HandleProperty (Binding b, string prop, ref string remaining)
		{
			char next;
			object value = null;
			string str_value = null;

			if (remaining.StartsWith ("{")) {
				value = ParseExpression (ref remaining);
				remaining = remaining.TrimStart ();

				if (remaining.Length > 0 && remaining[0] == ',')
					remaining = remaining.Substring (1);

				if (value is string)
					str_value = (string) value;
			}
			else {
				str_value = GetNextPiece (ref remaining, out next);
			}

			switch (prop) {
			case "FallbackValue":
				b.FallbackValue = value ?? str_value;
				break;
			case "Mode":
				if (str_value == null)
					throw new XamlParseException (String.Format ("Invalid type '{0}' for Mode.", value == null ? "null" : value.GetType ().ToString ()));
				b.Mode = (BindingMode) Enum.Parse (typeof (BindingMode), str_value, true);
				break;
			case "Path":
				if (str_value == null)
					throw new XamlParseException (String.Format ("Invalid type '{0}' for Path.", value == null ? "null" : value.GetType ().ToString ()));
				b.Path = new PropertyPath (str_value);
				break;
			case "Source":
				// if the expression was: Source="{StaticResource xxx}" then 'value' will be populated
				// If the expression was  Source="5" then 'str_value' will be populated.
				b.Source = value ?? str_value;
				break;
			case "StringFormat":
				b.StringFormat = (string) value ?? str_value;
				break;
			case "Converter":
				IValueConverter value_converter = value as IValueConverter;
				if (value_converter == null && value != null)
					throw new Exception ("A Binding Converter must be of type IValueConverter.");
				b.Converter = value_converter;
				break;
			case "ConverterParameter":
				b.ConverterParameter = value ?? str_value;
				break;
			case "NotifyOnValidationError":
				bool bl;
				if (!Boolean.TryParse (str_value, out bl))
					throw new Exception (String.Format ("Invalid value {0} for NotifyValidationOnError.", str_value));
				b.NotifyOnValidationError = bl;
				break;
			case "TargetNullValue":
				b.TargetNullValue = value ?? str_value;
				break;
			case "ValidatesOnExceptions":
				if (!Boolean.TryParse (str_value, out bl))
					throw new Exception (String.Format ("Invalid value {0} for ValidatesOnExceptions.", str_value));
				b.ValidatesOnExceptions = bl;
				break;
			case "RelativeSource":
				RelativeSource rs = value as RelativeSource;
				if (rs == null)
					throw new Exception (String.Format ("Invalid value {0} for RelativeSource.", value));
				 b.RelativeSource = rs;
				break;
			case "ElementName":
				b.ElementName = (string) value ?? str_value;
				break;
			case "UpdateSourceTrigger":
				b.UpdateSourceTrigger = (UpdateSourceTrigger) Enum.Parse (typeof (UpdateSourceTrigger), str_value, true);
				break;
			default:
				Console.Error.WriteLine ("Unhandled Binding Property:  '{0}'  value:  {1}", prop, value != null ? value.ToString () : str_value);
				break;
			}
		}

		private string GetNextPiece (ref string remaining, out char next)
		{
			bool inString = false;
			int end = 0;
			remaining = remaining.TrimStart ();
			piece = piece ?? new StringBuilder ();
			piece.Length = 0;

			// If we're inside a quoted string we append all chars to our piece until we hit the ending quote.
			while (end < remaining.Length && (inString || (remaining [end] != '}' && remaining [end] != ',' && remaining [end] != '='))) {
				if (remaining [end] == '\'')
					inString = !inString;

				// If this is an escape char, consume it and append the next char to our piece.
				if (remaining [end] == '\\') {
					end ++;
					if (end == remaining.Length)
						break;;
				}
				piece.Append (remaining [end]);
				end++;
			}

			if (end == 0) {
				next = Char.MaxValue;
				return null;
			}

			next = remaining [end];
			remaining = remaining.Substring (end + 1);

			// Whitespace is trimmed from the end of the piece before stripping
			// quote chars from the start/end of the string. 
			while (piece.Length > 0 && char.IsWhiteSpace (piece [piece.Length - 1]))
				piece.Length --;

			if (piece.Length >= 2 && piece [0] == '\'' && piece [piece.Length - 1] == '\'') {
				piece.Remove (piece.Length - 1, 1);
				piece.Remove (0, 1);
			}

			return piece.ToString ();
		}
	}
}


