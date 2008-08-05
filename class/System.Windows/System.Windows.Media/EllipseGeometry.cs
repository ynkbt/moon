//
// System.Windows.Media.EllipseGeometry class
//
// Contact:
//   Moonlight List (moonlight-list@lists.ximian.com)
//
// Copyright (C) 2007 Novell, Inc (http://www.novell.com)
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
using Mono;

namespace System.Windows.Media {

	public sealed class EllipseGeometry : Geometry {

		public static readonly DependencyProperty CenterProperty =
			DependencyProperty.Lookup (Kind.ELLIPSEGEOMETRY, "Center", typeof (Point));
		public static readonly DependencyProperty RadiusXProperty =
			DependencyProperty.Lookup (Kind.ELLIPSEGEOMETRY, "RadiusX", typeof (double));
		public static readonly DependencyProperty RadiusYProperty =
			DependencyProperty.Lookup (Kind.ELLIPSEGEOMETRY, "RadiusY", typeof (double));

		public EllipseGeometry () : base (NativeMethods.ellipse_geometry_new ())
		{
		}
		
		internal EllipseGeometry (IntPtr raw) : base (raw)
		{
		}

		public Point Center {
			get { return (Point) GetValue (CenterProperty); }
			set { SetValue (CenterProperty, value); }
		}

		public double RadiusX {
			get { return (double) GetValue (RadiusXProperty); }
			set { SetValue (RadiusXProperty, value); }
		}

		public double RadiusY {
			get { return (double) GetValue (RadiusYProperty); }
			set { SetValue (RadiusYProperty, value); }
		}

		internal override Kind GetKind ()
		{
			return Kind.ELLIPSEGEOMETRY;
		}
	}
}
