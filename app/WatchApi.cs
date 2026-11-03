// HTTP client for the watch's Programação mode (GET /api/slots, DELETE and POST /api/slot/N).
// The same API the web page uses. C# 5.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

public class WatchApi
{
    public string Base;   // "http://twatch.local" or "http://192.168.1.152"

    public WatchApi(string host) { Base = "http://" + host; }

    static HttpWebRequest NewRequest(string url, string method, int timeoutMs)
    {
        HttpWebRequest r = (HttpWebRequest)WebRequest.Create(url);
        r.Method = method;
        r.KeepAlive = false;
        r.Timeout = timeoutMs;
        r.ReadWriteTimeout = timeoutMs;
        r.Proxy = null;
        r.ServicePoint.Expect100Continue = false;
        return r;
    }

    static string ReadBody(WebResponse resp)
    {
        using (Stream s = resp.GetResponseStream())
        using (StreamReader sr = new StreamReader(s, Encoding.UTF8))
            return sr.ReadToEnd();
    }

    // Tries each host in turn (twatch.local, last IP, IP reported by the watch) and returns the first
    // that answers GET /api/slots, or null.
    public static WatchApi Find(IEnumerable<string> hosts, Action<string> log)
    {
        List<string> tried = new List<string>();
        foreach (string h in hosts)
        {
            if (string.IsNullOrEmpty(h) || h == "-" || tried.Contains(h)) continue;
            tried.Add(h);
            WatchApi api = new WatchApi(h);
            long free;
            string err;
            if (api.GetSlots(out free, out err) != null) { log("  " + h + ": responde"); return api; }
            log("  " + h + ": sem resposta");
        }
        return null;
    }

    public SlotInfo[] GetSlots(out long freeBytes, out string error)
    {
        freeBytes = 0;
        error = null;
        try
        {
            HttpWebRequest r = NewRequest(Base + "/api/slots", "GET", 4000);
            using (WebResponse resp = r.GetResponse())
            {
                string body = ReadBody(resp);
                JavaScriptSerializer js = new JavaScriptSerializer();
                Dictionary<string, object> d = js.Deserialize<Dictionary<string, object>>(body);
                freeBytes = Convert.ToInt64(d["free"]);
                ArrayList arr = (ArrayList)d["slots"];
                SlotInfo[] res = new SlotInfo[4];
                foreach (object o in arr)
                {
                    Dictionary<string, object> s = (Dictionary<string, object>)o;
                    int n = Convert.ToInt32(s["n"]);
                    if (n < 1 || n > 4) continue;
                    SlotInfo si = new SlotInfo();
                    si.N = n;
                    si.Size = Convert.ToInt64(s["size"]);
                    si.Name = s.ContainsKey("name") && s["name"] != null ? s["name"].ToString() : "";
                    res[n - 1] = si;
                }
                for (int i = 0; i < 4; i++) if (res[i] == null) { SlotInfo e = new SlotInfo(); e.N = i + 1; res[i] = e; }
                return res;
            }
        }
        catch (Exception e)
        {
            error = e.Message;
            return null;
        }
    }

    public bool Delete(int slot, out string error)
    {
        error = null;
        try
        {
            HttpWebRequest r = NewRequest(Base + "/api/slot/" + slot, "DELETE", 8000);
            using (WebResponse resp = r.GetResponse()) { ReadBody(resp); }
            return true;
        }
        catch (WebException e)
        {
            error = ErrorText(e);
            return false;
        }
    }

    // Waits until the watch answers again (after a failed upload it may take a while).
    public bool WaitUntilAnswers(int totalMs, CancellationToken ct)
    {
        System.Diagnostics.Stopwatch sw = System.Diagnostics.Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < totalMs && !ct.IsCancellationRequested)
        {
            long f; string e;
            if (GetSlots(out f, out e) != null) return true;
            Thread.Sleep(1500);
        }
        return false;
    }

    // The watch answers 400/507 with {"error":"..."} in Portuguese.
    static string ErrorText(WebException e)
    {
        try
        {
            if (e.Response != null)
            {
                string body = ReadBody(e.Response);
                Dictionary<string, object> d = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(body);
                if (d != null && d.ContainsKey("error")) return d["error"].ToString();
            }
        }
        catch (Exception) { }
        return e.Message;
    }

    // One upload attempt (multipart field "file", streamed, real progress). LinkProblem = the connection
    // broke (the watch keeps the previous slot, so the caller may retry).
    public PutResult Upload(string path, int slot, string displayName, Action<long> progress, CancellationToken ct,
                            out string message)
    {
        message = null;
        string name = AppLogic.SafeFileName(displayName);
        if (!name.EndsWith(".mp3", StringComparison.OrdinalIgnoreCase)) name += ".mp3";
        string boundary = "----twatch" + Guid.NewGuid().ToString("N");
        byte[] head = Encoding.UTF8.GetBytes("--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" +
                                             name + "\"\r\nContent-Type: audio/mpeg\r\n\r\n");
        byte[] tail = Encoding.ASCII.GetBytes("\r\n--" + boundary + "--\r\n");
        long fileLen = new FileInfo(path).Length;
        HttpWebRequest req = NewRequest(Base + "/api/slot/" + slot, "POST", 120000);
        req.AllowWriteStreamBuffering = false;
        req.ContentType = "multipart/form-data; boundary=" + boundary;
        req.ContentLength = head.Length + fileLen + tail.Length;
        try
        {
            using (Stream rs = req.GetRequestStream())
            using (FileStream fs = File.OpenRead(path))
            {
                rs.Write(head, 0, head.Length);
                byte[] buf = new byte[8192];
                long sent = 0;
                int n;
                while ((n = fs.Read(buf, 0, buf.Length)) > 0)
                {
                    if (ct.IsCancellationRequested)
                    {
                        req.Abort();
                        message = "Envio cancelado.";
                        return PutResult.Cancelled;
                    }
                    rs.Write(buf, 0, n);
                    sent += n;
                    if (progress != null) progress(sent);
                }
                rs.Write(tail, 0, tail.Length);
            }
            using (WebResponse resp = req.GetResponse()) { ReadBody(resp); }
            return PutResult.Ok;
        }
        catch (WebException e)
        {
            if (ct.IsCancellationRequested) { message = "Envio cancelado."; return PutResult.Cancelled; }
            HttpWebResponse hr = e.Response as HttpWebResponse;
            if (hr != null)
            {
                message = ErrorText(e);
                return PutResult.Refused;
            }
            message = "A ligação ao relógio caiu durante o envio (" + e.Message + ").";
            return PutResult.LinkProblem;
        }
        catch (IOException e)
        {
            message = "A ligação ao relógio caiu durante o envio (" + e.Message + ").";
            return PutResult.LinkProblem;
        }
    }
}
