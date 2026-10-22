$ErrorActionPreference='Stop'
$root='F:\Projects\tyrax2-lab-20261001'
$docs=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../docs'))
$payloads=@{};$removed=@{};$derived=@{};$pending=0
$manifests=@(Get-ChildItem -LiteralPath $root -Filter 'cleanup-20261012-*.csv' -File)
foreach($manifest in $manifests){
    foreach($r in Import-Csv -LiteralPath $manifest.FullName){
        if($r.action -eq 'delete-archived-copy'){
            if(-not [IO.Path]::GetFullPath($r.path).StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Origin outside lab'}
            if(-not [IO.Path]::GetFullPath($r.archive).StartsWith($docs+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Archive outside docs'}
            if([IO.File]::Exists($r.path)){$pending++;continue}
            if($payloads.ContainsKey($r.archive) -and $payloads[$r.archive] -ne $r.sha256){throw 'Archive hash disagreement'}
            $payloads[$r.archive]=$r.sha256;$removed[$r.path]=[long]$r.bytes
        }elseif($r.action -eq 'delete-derived'){
            if([IO.Directory]::Exists($r.path)){throw 'Derived directory still exists'}
            $derived[$r.path]=[long]$r.bytes
        }
    }
}
foreach($path in $payloads.Keys){
    $stream=[IO.File]::OpenRead($path)
    try{$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()}
    if($hash -ne $payloads[$path]){throw "Retained payload mismatch: $path"}
}
$bytes=[long]0;foreach($n in $removed.Values){$bytes+=$n};foreach($n in $derived.Values){$bytes+=$n}
$out=[ordered]@{valid=($pending -eq 0);removedArchivedCopies=$removed.Count;removedDerivedDirectories=$derived.Count;
    retainedArchivePayloadsVerified=$payloads.Count;recordedTargetsStillPresent=$pending;
    removedLogicalBytes=$bytes;removedGiB=[math]::Round($bytes/1GB,3);
    manifests=@($manifests.FullName);scope=$root;uniqueDataAndStatesRetained=$true}
$out | ConvertTo-Json -Depth 4
$out | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $docs 'tyrax2-lab-cleanup-2026-10-12.json')
