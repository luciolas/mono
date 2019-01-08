// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System.Collections.Generic;
using System.Net.Security;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Threading;
using System.Threading.Tasks;

namespace System.Net.Http
{
	interface IMonoHttpClientHandler : IDisposable
	{
		bool SupportsAutomaticDecompression {
			get;
		}

		bool UseCookies {
			get; set;
		}

		CookieContainer CookieContainer {
			get; set;
		}

		ClientCertificateOption ClientCertificateOptions {
			get; set;
		}

		X509CertificateCollection ClientCertificates {
			get;
		}

		Func<HttpRequestMessage, X509Certificate2, X509Chain, SslPolicyErrors, bool> ServerCertificateCustomValidationCallback {
			get; set;
		}

		bool CheckCertificateRevocationList {
			get; set;
		}

		SslProtocols SslProtocols {
			get; set;
		}

		DecompressionMethods AutomaticDecompression {
			get; set;
		}

		bool UseProxy {
			get; set;
		}

		IWebProxy Proxy {
			get; set;
		}

		ICredentials DefaultProxyCredentials {
			get; set;
		}

		long MaxRequestContentBufferSize {
			get; set;
		}

		bool PreAuthenticate {
			get; set;
		}

		bool UseDefaultCredentials {
			get; set;
		}

		ICredentials Credentials {
			get; set;
		}

		bool AllowAutoRedirect {
			get; set;
		}

		int MaxAutomaticRedirections {
			get; set;
		}

		int MaxConnectionsPerServer {
			get; set;
		}

		int MaxResponseHeadersLength {
			get; set;
		}

		IDictionary<string, object> Properties {
			get;
		}

		Task<HttpResponseMessage> SendAsync (HttpRequestMessage request, CancellationToken cancellationToken);
	}
}
