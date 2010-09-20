var Json = {
    encode: function (obj) {
        if (obj === null) {
            return "null";
        }
        else if (obj === undefined) {
            return "";
        }
        else {
	    switch (obj.constructor.name) {
	    case "String":
	        return '"' + obj.replace (/(["\\])/g, '\\$1') + '"';
	    case "Array":
	        return "[" + obj.map (Json.encode).join (",") + "]";
	    case "Object":
	        var string = [];
	        for (var property in obj)
                    string.push (Json.encode (property) + ":" + Json.encode (obj[property]));
	        return "{" + string.join (",") + "}";
	    case "Number":
	        if (isFinite (obj))
                    break;
	    case false:
	        return "null";
	    }
	    return String (obj);
        }
    },

    decode: function (str, secure) {
        return ((str.constructor != String) || (secure && ! (new RegExp (/^("(\\.|[^"\\\n\r])*?"|[,:{}\[\]0-9.\-+Eaeflnr-u \n\r\t])+?$/).test (str)))) ? null : eval ("(" + str + ")");
    }
};
