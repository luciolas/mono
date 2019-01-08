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
	public partial class HttpClientHandler : HttpMessageHandler
	{
		readonly IMonoHttpClientHandler _monoHandler;

		public HttpClientHandler () : this (new MonoWebRequestHandler ()) { }

		internal HttpClientHandler (IMonoHttpClientHandler monoHandler)
		{
			_monoHandler = monoHandler;
		}

		protected override void Dispose (bool disposing)
		{
			if (disposing) {
				_monoHandler.Dispose ();
			}
			base.Dispose (disposing);
		}

		public virtual bool SupportsAutomaticDecompression =>_monoHandler.SupportsAutomaticDecompression;

		public virtual bool SupportsProxy => true;

		public virtual bool SupportsRedirectConfiguration => true;

		public bool UseCookies {
			get => _monoHandler.UseCookies;
			set {
					_monoHandler.UseCookies = value;
			}
		}

		public CookieContainer CookieContainer {
			get => _monoHandler.CookieContainer;
			set => _monoHandler.CookieContainer = value;
		}

		public ClientCertificateOption ClientCertificateOptions {
			get => _monoHandler.ClientCertificateOptions;
			set => _monoHandler.ClientCertificateOptions = value;
		}

		public X509CertificateCollection ClientCertificates {
			get => _monoHandler.ClientCertificates;
		}

		public Func<HttpRequestMessage, X509Certificate2, X509Chain, SslPolicyErrors, bool> ServerCertificateCustomValidationCallback {
			get => _monoHandler.ServerCertificateCustomValidationCallback;
			set => _monoHandler.ServerCertificateCustomValidationCallback = value;
		}

		public bool CheckCertificateRevocationList {
			get => _monoHandler.CheckCertificateRevocationList;
			set => _monoHandler.CheckCertificateRevocationList = value;
		}

		public SslProtocols SslProtocols {
			get => _monoHandler.SslProtocols;
			set => _monoHandler.SslProtocols = value;
		}

		public DecompressionMethods AutomaticDecompression {
			get => _monoHandler.AutomaticDecompression;
			set => _monoHandler.AutomaticDecompression = value;
		}

		public bool UseProxy {
			get => _monoHandler.UseProxy;
			set => _monoHandler.UseProxy = value;
		}

		public IWebProxy Proxy {
			get => _monoHandler.Proxy;
			set => _monoHandler.Proxy = value;
		}

		public ICredentials DefaultProxyCredentials {
			get => _monoHandler.DefaultProxyCredentials;
			set => _monoHandler.DefaultProxyCredentials = value;
		}

		public long MaxRequestContentBufferSize {
			get => _monoHandler.MaxRequestContentBufferSize;
			set => _monoHandler.MaxRequestContentBufferSize = value;
		}

		public bool PreAuthenticate {
			get => _monoHandler.PreAuthenticate;
			set => _monoHandler.PreAuthenticate = value;
		}

		public bool UseDefaultCredentials {
			// Either read variable from curlHandler or compare .Credentials as socketsHttpHandler does not have separate prop.
			get => _monoHandler.UseDefaultCredentials;
			set => _monoHandler.UseDefaultCredentials = value;
		}

		public ICredentials Credentials {
			get => _monoHandler.Credentials;
			set => _monoHandler.Credentials = value;
		}

		public bool AllowAutoRedirect {
			get => _monoHandler.AllowAutoRedirect;
			set => _monoHandler.AllowAutoRedirect = value;
		}

		public int MaxAutomaticRedirections {
			get => _monoHandler.MaxAutomaticRedirections;
			set => _monoHandler.MaxAutomaticRedirections = value;
		}

		public int MaxConnectionsPerServer {
			get => _monoHandler.MaxConnectionsPerServer;
			set => _monoHandler.MaxConnectionsPerServer = value;
		}

		public int MaxResponseHeadersLength {
			get => _monoHandler.MaxResponseHeadersLength;
			set => _monoHandler.MaxResponseHeadersLength = value;
		}

		public IDictionary<string, object> Properties => _monoHandler.Properties;

		protected internal override Task<HttpResponseMessage> SendAsync (HttpRequestMessage request, CancellationToken cancellationToken) =>
		    _monoHandler.SendAsync (request, cancellationToken);
	}
}
