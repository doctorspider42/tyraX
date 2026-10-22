param([switch]$Apply,[switch]$RootBinaries)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath('F:\Projects\tyrax2-lab-20261001').TrimEnd('\')
$docs=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../docs'))
if(-not (Test-Path -LiteralPath $root -PathType Container)){throw 'Lab missing'}
if((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Lab is a link'}
$opts=[IO.EnumerationOptions]::new()
$opts.RecurseSubdirectories=$true
$opts.AttributesToSkip=[IO.FileAttributes]::ReparsePoint
$opts.IgnoreInaccessible=$false
function Assert-LabPath([string]$path){
    $full=[IO.Path]::GetFullPath($path)
    if(-not $full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Outside lab: $full"}
    if([IO.File]::GetAttributes($full) -band [IO.FileAttributes]::ReparsePoint){throw "Link: $full"}
    return $full
}
function Digest([string]$path){
    $stream=[IO.File]::OpenRead($path)
    try{return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))}
    finally{$stream.Dispose()}
}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$report=Join-Path $root "cleanup-$stamp.csv"
$writer=[IO.StreamWriter]::new($report,$false,[Text.UTF8Encoding]::new($false))
$writer.WriteLine('action,path,archive,sha256,bytes')
function Record($action,$path,$archive,$hash,$bytes){
    $values=@($action,$path,$archive,$hash,[string]$bytes) | ForEach-Object {'"'+([string]$_).Replace('"','""')+'"'}
    $writer.WriteLine($values -join ',');$writer.Flush()
}
$removedBytes=[long]0;$removedFiles=0;$removedDirs=0;$duplicates=0;$kept=0
try{
    $derived=if($RootBinaries){@()}else{@([IO.Directory]::EnumerateDirectories($root,'*',$opts) | Where-Object {
        [IO.Path]::GetFileName($_) -in @('obj','__pycache__') -and $_ -notmatch '\\.git(\\|$)'
    })}
    foreach($dir in $derived){
        if(-not (Test-Path -LiteralPath $dir)){continue}
        $target=Assert-LabPath $dir
        $nestedLinks=@(Get-ChildItem -LiteralPath $target -Force -Recurse | Where-Object {
            $_.Attributes -band [IO.FileAttributes]::ReparsePoint
        })
        if($nestedLinks.Count){Record 'keep-linked-tree' $target '' '' 0;continue}
        $files=@([IO.Directory]::EnumerateFiles($target,'*',$opts))
        $size=[long]0;foreach($file in $files){$size+=([IO.FileInfo]::new($file)).Length}
        Record $(if($Apply){'delete-derived'}else{'plan-derived'}) $target '' '' $size
        if($Apply){Remove-Item -LiteralPath $target -Recurse -Force; $removedBytes+=$size;$removedFiles+=$files.Count;$removedDirs++}
    }
    Write-Output "Derived directories handled: $($derived.Count); building archive index"
    $index=@{}
    foreach($path in [IO.Directory]::EnumerateFiles($docs,'*',$opts)){
        $f=[IO.FileInfo]::new($path);$key=if($RootBinaries){[string]$f.Length}else{$f.Name.ToLowerInvariant()+'|'+$f.Length}
        if(-not $index.ContainsKey($key)){$index[$key]=[Collections.Generic.List[string]]::new()}
        $index[$key].Add($path)
    }
    $hashes=@{};$seen=0
    $labFiles=if($RootBinaries){@(Get-ChildItem -LiteralPath $root -File | Where-Object {
        $_.Extension -in @('.elf','.sym','.a') -and -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)
    } | ForEach-Object {$_.FullName})}else{@([IO.Directory]::EnumerateFiles($root,'*',$opts))}
    foreach($path in $labFiles){
        $seen++
        if($seen%10000 -eq 0){Write-Output "Checked $seen/$($labFiles.Count); archived duplicates $duplicates; freed GiB $([math]::Round($removedBytes/1GB,2))"}
        if($path -match '\\.git(\\|$)' -or [IO.Path]::GetFileName($path) -like 'cleanup-*'){continue}
        $file=[IO.FileInfo]::new($path);$key=if($RootBinaries){[string]$file.Length}else{$file.Name.ToLowerInvariant()+'|'+$file.Length}
        if(-not $index.ContainsKey($key)){$kept++;continue}
        $digest=Digest $path
        $match=$null
        foreach($candidate in $index[$key]){
            if(-not $hashes.ContainsKey($candidate)){$hashes[$candidate]=Digest $candidate}
            if($hashes[$candidate] -eq $digest){$match=$candidate;break}
        }
        if(-not $match){$kept++;continue}
        $target=Assert-LabPath $path
        # Recheck both payloads immediately before deletion; names alone never qualify.
        if((Digest $target) -ne $digest -or (Digest $match) -ne $digest){throw 'Payload changed during cleanup'}
        Record $(if($Apply){'delete-archived-copy'}else{'plan-archived-copy'}) $target $match $digest $file.Length
        $duplicates++
        if($Apply){
            $attrs=[IO.File]::GetAttributes($target)
            if($attrs -band [IO.FileAttributes]::ReadOnly){[IO.File]::SetAttributes($target,$attrs -band (-bnot [IO.FileAttributes]::ReadOnly))}
            [IO.File]::Delete($target);$removedBytes+=$file.Length;$removedFiles++
        }
    }
    if($Apply -and -not $RootBinaries){
        foreach($dir in [IO.Directory]::EnumerateDirectories($root,'*',$opts) | Sort-Object Length -Descending){
            if($dir -match '\\.git(\\|$)'){continue}
            if(-not [IO.Directory]::EnumerateFileSystemEntries($dir).GetEnumerator().MoveNext()){
                $target=Assert-LabPath $dir;Remove-Item -LiteralPath $target -Force;$removedDirs++
            }
        }
    }
    [pscustomobject]@{applied=[bool]$Apply;removedFiles=$removedFiles;removedDirectories=$removedDirs;
        archivedDuplicates=$duplicates;keptUniqueOrUnmatched=$kept;freedBytes=$removedBytes;manifest=$report} | ConvertTo-Json
}finally{$writer.Dispose()}
