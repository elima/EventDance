const MainLoop = imports.mainloop;
const Glib     = imports.gi.GLib;
const Evd      = imports.gi.Evd;

const Assert   = imports.common.assert;
const Test     = imports.common.test;

function testInitialState (Assert) {
    let cert = new Evd.TlsCertificate ();
    Assert.ok (cert);

    Assert.equal (cert.type, Evd.TlsCertificateType.UNKNOWN);

    let dn = null;
    try {
        dn = cert.get_dn ();
    }
    catch (e) {
        Assert.ok (e);
    }
    Assert.equal (dn, null);
}

function testX509Import (Assert) {
    let cert = new Evd.TlsCertificate ();

    let [result, rawPem] = Glib.file_get_contents ("certs/x509-server.pem");

    Assert.ok (cert.import (rawPem, rawPem.length));
    Assert.equal (cert.get_dn (), "O=EventDance,CN=eventdance.org");
    Assert.equal (cert.type, Evd.TlsCertificateType.X509);

    let [result, rawPem] = Glib.file_get_contents ("certs/x509-jane.pem");
    Assert.ok (cert.import (rawPem, rawPem.length));
    Assert.equal (cert.get_dn (), "CN=Jane");
    Assert.equal (cert.type, Evd.TlsCertificateType.X509);
}

Evd.tls_init ();

Test.run (this);

Evd.tls_deinit ();
