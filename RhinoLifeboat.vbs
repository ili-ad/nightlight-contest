Option Explicit

Dim shell, desktop, outPath
Set shell = CreateObject("WScript.Shell")
desktop = shell.SpecialFolders("Desktop")
outPath = desktop & "\lifeboat_from_expired_rhino.obj"

Dim app, rh
On Error Resume Next
Set app = GetObject(, "Rhino.Interface")
If Err.Number <> 0 Or app Is Nothing Then
    Err.Clear
    Set app = CreateObject("Rhino.Interface")
End If

If Err.Number <> 0 Or app Is Nothing Then
    MsgBox "Could not attach to Rhino. " & Err.Description
    WScript.Quit 1
End If

Set rh = app.GetScriptObject()
If Err.Number <> 0 Or rh Is Nothing Then
    MsgBox "Could not get RhinoScript automation object. " & Err.Description
    WScript.Quit 1
End If
On Error GoTo 0

Dim objs
On Error Resume Next
objs = rh.SelectedObjects(False, False)
If Err.Number <> 0 Then
    MsgBox "Could not read selected objects. Rhino may be blocking automation. " & Err.Description
    WScript.Quit 1
End If
On Error GoTo 0

If Not IsArray(objs) Then
    MsgBox "No selected objects found. Select the polysurface in Rhino first, then run this again."
    WScript.Quit 1
End If

Dim fso, ts
Set fso = CreateObject("Scripting.FileSystemObject")
Set ts = fso.CreateTextFile(outPath, True)

ts.WriteLine "# Lifeboat OBJ from expired Rhino session"
ts.WriteLine "# Mesh rescue only. Coordinates are raw Rhino coordinates."

Dim vertexOffset, meshCount, failCount
vertexOffset = 0
meshCount = 0
failCount = 0

Dim obj, meshThing, mid

For Each obj In objs
    On Error Resume Next

    If rh.IsMesh(CStr(obj)) Then
        If Err.Number = 0 Then
            WriteMesh rh, ts, CStr(obj), vertexOffset, meshCount
        Else
            failCount = failCount + 1
            Err.Clear
        End If
    Else
        Err.Clear
        meshThing = rh.ExtractRenderMesh(CStr(obj))

        If Err.Number <> 0 Then
            failCount = failCount + 1
            Err.Clear
        ElseIf IsArray(meshThing) Then
            For Each mid In meshThing
                WriteMesh rh, ts, CStr(mid), vertexOffset, meshCount
            Next
        ElseIf VarType(meshThing) = vbString Then
            WriteMesh rh, ts, CStr(meshThing), vertexOffset, meshCount
        Else
            failCount = failCount + 1
        End If
    End If

    On Error GoTo 0
Next

ts.Close

If meshCount > 0 Then
    MsgBox "Lifeboat OBJ written:" & vbCrLf & outPath & vbCrLf & vbCrLf & _
           "Meshes written: " & meshCount & vbCrLf & _
           "Failures: " & failCount & vbCrLf & vbCrLf & _
           "Open/import this OBJ in Rhino 8 using the same units."
Else
    MsgBox "No mesh data was written. Rhino probably blocked automation too."
End If

Sub WriteMesh(rh, ts, meshId, ByRef vertexOffset, ByRef meshCount)
    Dim verts, faces, i, p, face

    On Error Resume Next
    verts = rh.MeshVertices(meshId)
    faces = rh.MeshFaceVertices(meshId)
    If Err.Number <> 0 Then
        Err.Clear
        Exit Sub
    End If
    On Error GoTo 0

    If Not IsArray(verts) Then Exit Sub
    If Not IsArray(faces) Then Exit Sub

    meshCount = meshCount + 1
    ts.WriteLine "o lifeboat_mesh_" & CStr(meshCount)

    For i = 0 To UBound(verts)
        p = verts(i)
        ts.WriteLine "v " & Num(p(0)) & " " & Num(p(1)) & " " & Num(p(2))
    Next

    For i = 0 To UBound(faces)
        face = faces(i)

        If CLng(face(2)) = CLng(face(3)) Then
            ts.WriteLine "f " & _
                CStr(vertexOffset + CLng(face(0)) + 1) & " " & _
                CStr(vertexOffset + CLng(face(1)) + 1) & " " & _
                CStr(vertexOffset + CLng(face(2)) + 1)
        Else
            ts.WriteLine "f " & _
                CStr(vertexOffset + CLng(face(0)) + 1) & " " & _
                CStr(vertexOffset + CLng(face(1)) + 1) & " " & _
                CStr(vertexOffset + CLng(face(2)) + 1) & " " & _
                CStr(vertexOffset + CLng(face(3)) + 1)
        End If
    Next

    vertexOffset = vertexOffset + UBound(verts) + 1
End Sub

Function Num(x)
    Num = Replace(CStr(CDbl(x)), ",", ".")
End Function