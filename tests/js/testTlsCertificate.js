const MainLoop = imports.mainloop;
const Glib     = imports.gi.GLib;
const Evd      = imports.gi.Evd;

const Assert   = imports.common.assert;
const Test     = imports.common.test;

function testInitialState (Assert) {
    let cert = new Evd.TlsCertificate ();
    Assert.ok (cert);

    Assert.equal (cert.type, Evd.TlsCertificateType.UNKNOWN);

    // when in uninitialized state, all property getters return error
    let dn = null;
    try {
        dn = cert.get_dn ();
    }
    catch (e) {
        Assert.ok (e);
    }
    Assert.equal (dn, null);

    let time = -1;
    try {
        time = cert.get_activation_time ();
    }
    catch (e) {
        Assert.ok (e);
    }
    Assert.equal (time, -1);

    try {
        time = cert.get_expiration_time ();
    }
    catch (e) {
        Assert.ok (e);
    }
    Assert.equal (time, -1);
}

function testX509Import (Assert) {
    let cert = new Evd.TlsCertificate ();

    let [result, rawPem] = Glib.file_get_contents ("certs/x509-server.pem");
    Assert.ok (cert.import (rawPem, rawPem.length));
    Assert.equal (cert.type, Evd.TlsCertificateType.X509);
    Assert.equal (cert.get_dn (), "O=EventDance,CN=eventdance.org");
    Assert.equal (cert.get_expiration_time () * 1000, new Date ("Wed Mar 23 2011 15:43:49 GMT+0100 (CET)").valueOf ());
    Assert.equal (cert.get_activation_time () * 1000, new Date ("Tue Mar 23 2010 15:43:49 GMT+0100 (CET)").valueOf ());
    Assert.equal (cert.verify_validity (), Evd.TlsVerifyState.OK);

    let [result, rawPem] = Glib.file_get_contents ("certs/x509-jane.pem");
    Assert.ok (cert.import (rawPem, rawPem.length));
    Assert.equal (cert.type, Evd.TlsCertificateType.X509);
    Assert.equal (cert.get_dn (), "CN=Jane");
    Assert.equal (cert.get_expiration_time () * 1000, new Date ("Wed Mar 23 2011 15:48:25 GMT+0100 (CET)").valueOf ());
    Assert.equal (cert.get_activation_time () * 1000, new Date ("Tue Mar 23 2010 15:48:25 GMT+0100 (CET)").valueOf ());
    Assert.equal (cert.verify_validity (), Evd.TlsVerifyState.OK);

    let [result, rawPem] = Glib.file_get_contents ("certs/openpgp-server.asc");
    Assert.ok (cert.import (rawPem, rawPem.length));
    Assert.equal (cert.type, Evd.TlsCertificateType.OPENPGP);
    Assert.equal (cert.get_dn (), "EventDance (Evd) <test@eventdance.org>");
    Assert.equal (cert.get_expiration_time () * 1000, new Date ("Tue May 12 2015 16:13:11 GMT+0200 (CET)").valueOf ());
    Assert.equal (cert.get_activation_time () * 1000, new Date ("Thu May 13 2010 16:13:11 GMT+0200 (CET)").valueOf ());
    Assert.equal (cert.verify_validity (), Evd.TlsVerifyState.OK);
}

Evd.tls_init ();

Test.run (this);

Evd.tls_deinit ();
